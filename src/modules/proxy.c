

// src/modules/proxy.c
#include "proxy.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include "../utils/logging.h"
#include <errno.h>     // Added for errno

#define BUFFER_SIZE 8192

proxy_config_t* proxy_init(void) {
    proxy_config_t* config = malloc(sizeof(proxy_config_t));
    if (config) {
        config->backend_count = 0;
        memset(config->backends, 0, sizeof(config->backends));
    }
    return config;
}

int proxy_add_backend(proxy_config_t* config, const char* ip, int port) {
    if (config->backend_count >= MAX_BACKENDS) {
        log_error("Maximum number of backends reached");
        return -1;
    }

    backend_server_t* backend = &config->backends[config->backend_count];
    
    // Copy IP address
    strncpy(backend->ip, ip, sizeof(backend->ip) - 1);
    backend->port = port;

    // Setup backend address
    memset(&backend->addr, 0, sizeof(backend->addr));
    backend->addr.sin_family = AF_INET;
    backend->addr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip, &backend->addr.sin_addr) <= 0) {
        log_error("Invalid backend IP address");
        return -1;
    }

    config->backend_count++;
    log_info("Added backend server %s:%d", ip, port);
    return 0;
}
int proxy_handle_request(int client_fd, proxy_config_t* config) {
    if (config->backend_count == 0) {
        log_error("No backend servers configured");
        return -1;
    }

    backend_server_t* backend = &config->backends[0];
    
    // Create connection to backend
    int backend_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (backend_fd < 0) {
        log_error("Failed to create backend socket: %s", strerror(errno));
        return -1;
    }

    // Set timeout for both read and write operations
    struct timeval timeout;
    timeout.tv_sec = HTTP_TIMEOUT_SECONDS;  // Now defined in proxy.h
    timeout.tv_usec = 0;
    setsockopt(backend_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(backend_fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    // Connect to backend
    if (connect(backend_fd, (struct sockaddr*)&backend->addr, sizeof(backend->addr)) < 0) {
        log_error("Failed to connect to backend %s:%d - %s", 
                 backend->ip, backend->port, strerror(errno));
        close(backend_fd);
        return -1;
    }

    char buffer[BUFFER_SIZE];
    ssize_t bytes_read;
    size_t total_sent = 0;
    
    // Forward client request to backend
    while ((bytes_read = read(client_fd, buffer, sizeof(buffer))) > 0) {
        ssize_t bytes_written = write(backend_fd, buffer, bytes_read);
        if (bytes_written < 0) {
            log_error("Failed to forward request to backend: %s", strerror(errno));
            close(backend_fd);
            return -1;
        }
        
        total_sent += bytes_written;
        
        // Check for end of HTTP request
        if (strstr(buffer, "\r\n\r\n")) {
            break;
        }
    }

    if (bytes_read < 0) {
        log_error("Failed to read from client: %s", strerror(errno));
        close(backend_fd);
        return -1;
    }

    log_info("Forwarded %zu bytes to backend %s:%d", 
             total_sent, backend->ip, backend->port);

    // Forward backend response to client
    size_t total_received = 0;
    while ((bytes_read = read(backend_fd, buffer, sizeof(buffer))) > 0) {
        ssize_t bytes_written = write(client_fd, buffer, bytes_read);
        if (bytes_written < 0) {
            log_error("Failed to forward response to client: %s", strerror(errno));
            break;
        }
        total_received += bytes_written;
    }

    log_info("Received %zu bytes from backend", total_received);
    
    close(backend_fd);
    return 0;
}

void proxy_cleanup(proxy_config_t* config) {
    if (config) {
        free(config);
    }
}