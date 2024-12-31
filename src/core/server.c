#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define SSL_HANDSHAKE_PENDING 3
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/wait.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <arpa/inet.h>
#include <sys/epoll.h>

#define MAX_EVENTS 10
#define WORKER_COUNT 4
#define BUFFER_SIZE 8192
#define MAX_HEADERS 100
#define MAX_HEADER_SIZE 8192
#define LISTEN_PORT 8080
#define BACKEND_PORT 3000
#define MAX_PENDING 1024
#define TIMEOUT_SECONDS 30

typedef struct {
    char* key;
    char* value;
} Header;

typedef struct {
    char method[16];
    char path[2048];
    char version[16];
    Header headers[MAX_HEADERS];
    int header_count;
    char* body;
    size_t body_length;
    size_t content_length;
} HttpRequest;

typedef struct {
    SSL_CTX* ctx;
    int client_fd;
    SSL* ssl;
    HttpRequest request;
    int backend_fd;
    char buffer[BUFFER_SIZE];
    size_t buffer_used;
    int state;  // 0: reading request, 1: connecting backend, 2: forwarding
int handshake_completed;
} Connection;


static volatile int running = 1;

// Function that prints OpenSSL errors - moved up before it's used
void print_ssl_error(const char* context) {
    unsigned long err;
    char err_buf[256];
    
    while ((err = ERR_get_error()) != 0) {
        ERR_error_string_n(err, err_buf, sizeof(err_buf));
        fprintf(stderr, "%s: %s\n", context, err_buf);
    }
}


void log_error(const char* message, int err_no) {
    time_t now;
    time(&now);
    char* time_str = ctime(&now);
    time_str[strlen(time_str) - 1] = '\0';
    
    if (err_no != 0) {
        fprintf(stderr, "[%s] ERROR: %s: %s\n", time_str, message, strerror(err_no));
    } else {
        fprintf(stderr, "[%s] ERROR: %s\n", time_str, message);
    }
}

SSL_CTX* initialize_ssl() {
    SSL_METHOD *method;
    SSL_CTX *ctx;

    OpenSSL_add_ssl_algorithms();
    SSL_load_error_strings();
    method = (SSL_METHOD *)SSLv23_server_method();
    ctx = SSL_CTX_new(method);

    if (!ctx) {
        ERR_print_errors_fp(stderr);
        exit(EXIT_FAILURE);
    }

    if (SSL_CTX_use_certificate_file(ctx, "server.crt", SSL_FILETYPE_PEM) <= 0) {
        ERR_print_errors_fp(stderr);
        exit(EXIT_FAILURE);
    }

    if (SSL_CTX_use_PrivateKey_file(ctx, "server.key", SSL_FILETYPE_PEM) <= 0) {
        ERR_print_errors_fp(stderr);
        exit(EXIT_FAILURE);
    }

    if (!SSL_CTX_check_private_key(ctx)) {
        fprintf(stderr, "Private key does not match the certificate\n");
        exit(EXIT_FAILURE);
    }

    return ctx;
}
void cleanup_connection(Connection* conn) {
    if (conn->ssl) {
        SSL_shutdown(conn->ssl);
        SSL_free(conn->ssl);
    }
    if (conn->client_fd >= 0) close(conn->client_fd);
    if (conn->backend_fd >= 0) close(conn->backend_fd);
    if (conn->request.body) free(conn->request.body);
    for (int i = 0; i < conn->request.header_count; i++) {
        free(conn->request.headers[i].key);
        free(conn->request.headers[i].value);
    }
    free(conn);
}


int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int parse_http_request(Connection* conn) {
    char* current = conn->buffer;
    char* end = conn->buffer + conn->buffer_used;
    char* line_end;

    // Parse request line
    line_end = strstr(current, "\r\n");
    if (!line_end) return 0;  // Need more data

    if (sscanf(current, "%15s %2047s %15s", 
               conn->request.method, 
               conn->request.path, 
               conn->request.version) != 3) {
        return -1;
    }

    current = line_end + 2;

    // Parse headers
    while (current < end && conn->request.header_count < MAX_HEADERS) {
        line_end = strstr(current, "\r\n");
        if (!line_end) return 0;  // Need more data

        if (current == line_end) {
            // Empty line, end of headers
            current += 2;
            break;
        }

        char* colon = memchr(current, ':', line_end - current);
        if (!colon) return -1;

        int key_len = colon - current;
        int value_len = line_end - (colon + 2);

        conn->request.headers[conn->request.header_count].key = malloc(key_len + 1);
        conn->request.headers[conn->request.header_count].value = malloc(value_len + 1);

        memcpy(conn->request.headers[conn->request.header_count].key, current, key_len);
        conn->request.headers[conn->request.header_count].key[key_len] = '\0';

        memcpy(conn->request.headers[conn->request.header_count].value, colon + 2, value_len);
        conn->request.headers[conn->request.header_count].value[value_len] = '\0';

        conn->request.header_count++;
        current = line_end + 2;
    }

    // Look for Content-Length
    for (int i = 0; i < conn->request.header_count; i++) {
        if (strcasecmp(conn->request.headers[i].key, "Content-Length") == 0) {
            conn->request.content_length = atol(conn->request.headers[i].value);
            break;
        }
    }

    // Handle body if present
    if (conn->request.content_length > 0) {
        size_t body_received = end - current;
        if (body_received < conn->request.content_length) {
            return 0;  // Need more data
        }
        conn->request.body = malloc(conn->request.content_length);
        memcpy(conn->request.body, current, conn->request.content_length);
        conn->request.body_length = conn->request.content_length;
    }

    return 1;  // Parsing complete
}

int handle_client_data(Connection* conn) {
    if (conn->state == 0) {  // Reading request
        ssize_t bytes = SSL_read(conn->ssl, 
                                conn->buffer + conn->buffer_used,
                                BUFFER_SIZE - conn->buffer_used);
        
        if (bytes <= 0) {
            int err = SSL_get_error(conn->ssl, bytes);
            if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
                return 0;  // Need more data
            }
            return -1;  // Error
        }

        conn->buffer_used += bytes;
        
        int parse_result = parse_http_request(conn);
        if (parse_result == 0) return 0;  // Need more data
        if (parse_result < 0) return -1;  // Parse error

        // Start backend connection
        conn->backend_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
        if (conn->backend_fd < 0) return -1;

        struct sockaddr_in backend_addr = {
            .sin_family = AF_INET,
            .sin_port = htons(BACKEND_PORT),
            .sin_addr.s_addr = inet_addr("127.0.0.1")
        };

        if (connect(conn->backend_fd, (struct sockaddr*)&backend_addr, 
                   sizeof(backend_addr)) < 0) {
            if (errno != EINPROGRESS) return -1;
        }

        conn->state = 1;  // Connecting to backend
        return 0;
    }

    return 0;
}

void handle_backend_data(Connection* conn) {
    char buffer[BUFFER_SIZE];
    ssize_t bytes = recv(conn->backend_fd, buffer, BUFFER_SIZE, 0);
    
    if (bytes > 0) {
        SSL_write(conn->ssl, buffer, bytes);
    } else if (bytes == 0 || (bytes < 0 && errno != EAGAIN)) {
        cleanup_connection(conn);
    }
}


void worker_process(int server_fd, SSL_CTX* ssl_ctx) {
    int epoll_fd = epoll_create1(0);
    if (epoll_fd < 0) {
        log_error("Epoll creation failed", errno);
        return;
    }

    struct epoll_event ev, events[MAX_EVENTS];
    ev.events = EPOLLIN;
    ev.data.fd = server_fd;
    
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &ev) < 0) {
        log_error("Failed to add server socket to epoll", errno);
        return;
    }

    while (running) {
        int nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        
        for (int n = 0; n < nfds; n++) {
            if (events[n].data.fd == server_fd) {
                // New connection
                struct sockaddr_in client_addr;
                socklen_t client_len = sizeof(client_addr);
                
                int client_fd = accept(server_fd, 
                                     (struct sockaddr*)&client_addr,
                                     &client_len);
                
                if (client_fd < 0) {
                    if (errno != EAGAIN && errno != EWOULDBLOCK) {
                        log_error("Accept failed", errno);
                    }
                    continue;
                }

                printf("New client connection from %s:%d\n", 
                       inet_ntoa(client_addr.sin_addr),
                       ntohs(client_addr.sin_port));

                if (set_nonblocking(client_fd) < 0) {
                    log_error("Failed to set nonblocking", errno);
                    close(client_fd);
                    continue;
                }

                Connection* conn = calloc(1, sizeof(Connection));
                if (!conn) {
                    log_error("Failed to allocate connection", errno);
                    close(client_fd);
                    continue;
                }

                conn->ctx = ssl_ctx;
                conn->client_fd = client_fd;
                conn->backend_fd = -1;
                conn->ssl = SSL_new(ssl_ctx);
                
                if (!conn->ssl) {
                    fprintf(stderr, "Failed to create SSL structure\n");
                    print_ssl_error("SSL_new");
                    cleanup_connection(conn);
                    continue;
                }

                if (!SSL_set_fd(conn->ssl, client_fd)) {
                    fprintf(stderr, "Failed to set SSL file descriptor\n");
                    print_ssl_error("SSL_set_fd");
                    cleanup_connection(conn);
                    continue;
                }
                
                printf("Starting SSL handshake...\n");
                int ret = SSL_accept(conn->ssl);
                if (ret <= 0) {
                    int ssl_err = SSL_get_error(conn->ssl, ret);
                    fprintf(stderr, "SSL handshake failed (error: %d)\n", ssl_err);
                    print_ssl_error("SSL_accept");
                    cleanup_connection(conn);
                    continue;
                }
                printf("SSL handshake completed successfully with %s encryption\n",
                       SSL_get_cipher(conn->ssl));

                ev.events = EPOLLIN | EPOLLET;
                ev.data.ptr = conn;
                if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &ev) < 0) {
                    log_error("Failed to add to epoll", errno);
                    cleanup_connection(conn);
                    continue;
                }
            } else {
                // Existing connection
                Connection* conn = events[n].data.ptr;
                
                if (events[n].events & EPOLLIN) {
                    if (conn->state == 0 || conn->state == 1) {
                        if (handle_client_data(conn) < 0) {
                            cleanup_connection(conn);
                            continue;
                        }
                    } else {
                        handle_backend_data(conn);
                    }
                }
                
                if (events[n].events & (EPOLLERR | EPOLLHUP)) {
                    cleanup_connection(conn);
                }
            }
        }
    }

    close(epoll_fd);
}


void signal_handler(int sig) {
    if (sig == SIGTERM || sig == SIGINT) {
        running = 0;
    }
}
int handle_ssl_handshake(Connection* conn) {
    if (conn->handshake_completed) {
        return 1;  // Already completed
    }

    int ret = SSL_accept(conn->ssl);
    if (ret == 1) {
        // Handshake completed successfully
        conn->handshake_completed = 1;
        printf("SSL handshake completed successfully with %s encryption\n",
               SSL_get_cipher(conn->ssl));
        return 1;
    }

    int ssl_err = SSL_get_error(conn->ssl, ret);
    switch (ssl_err) {
        case SSL_ERROR_WANT_READ:
        case SSL_ERROR_WANT_WRITE:
            // Need more data, will retry later
            return 0;
        default:
            // Fatal error
            fprintf(stderr, "SSL handshake failed (error: %d)\n", ssl_err);
            print_ssl_error("SSL_accept");
            return -1;
    }
}


int main() {
    SSL_library_init();
    
    // Initialize SSL
    SSL_CTX* ssl_ctx = initialize_ssl();

    // Create server socket
    int server_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (server_fd < 0) {
        log_error("Socket creation failed", errno);
        exit(EXIT_FAILURE);
    }

    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        log_error("Setsockopt failed", errno);
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in address = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = INADDR_ANY,
        .sin_port = htons(LISTEN_PORT)
    };

    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        log_error("Bind failed", errno);
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, MAX_PENDING) < 0) {
        log_error("Listen failed", errno);
        exit(EXIT_FAILURE);
    }

    // Set up signal handlers
    signal(SIGTERM, signal_handler);
    signal(SIGINT, signal_handler);
    signal(SIGPIPE, SIG_IGN);

    printf("Master process started. PID: %d\n", getpid());
    printf("Listening on port %d (HTTPS), forwarding to port %d\n", 
           LISTEN_PORT, BACKEND_PORT);

    // Create worker processes
    for (int i = 0; i < WORKER_COUNT; i++) {
        pid_t pid = fork();

        if (pid == 0) {
            printf("Worker process %d started\n", i + 1);
            worker_process(server_fd, ssl_ctx);
            SSL_CTX_free(ssl_ctx);
            exit(0);
        } else if (pid < 0) {
            log_error("Fork failed", errno);
            exit(EXIT_FAILURE);
        }
    }

    // Master process loop
    while (running) {
        sleep(1);
    }

    // Cleanup
    close(server_fd);
    SSL_CTX_free(ssl_ctx);
    return 0;
}