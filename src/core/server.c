// src/core/server.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include "../modules/proxy.h"
#include "../utils/logging.h"  // You'll need to implement this

#define MAX_WORKERS 4
#define DEFAULT_PORT 5000
#define LISTEN_BACKLOG 1024

typedef struct {
    int port;
    int worker_count;
    proxy_config_t* proxy_config;
} server_config_t;

static int server_fd;
static pid_t worker_pids[MAX_WORKERS];

// Signal handler for graceful shutdown
static void handle_signal(int sig) {
//Purpose: Handles graceful shutdown when server receives termination signals
// SIGTERM: Terminal's "kill" command signal
// SIGINT: Ctrl+C interrupt signal
    if (sig == SIGTERM || sig == SIGINT) {
        // Close the server socket
        if (server_fd > 0) {
            close(server_fd);
        }//Closes main server socket if it's open
         //Prevents new connections from being accepted
        
        
        // Kill all worker processes
        for (int i = 0; i < MAX_WORKERS; i++) {
            if (worker_pids[i] > 0) {
                kill(worker_pids[i], SIGTERM);
            }
        }
// Loops through all worker process IDs
// Sends termination signal to each active worker
// worker_pids array contains PIDs of all worker processes
        
        // Wait for all workers to finish
        while (wait(NULL) > 0);
        exit(0);//prevents the zombie process from being created
    }
}

// Worker process main loop
static void worker_process(server_config_t* config) {
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    int client_fd;
// Sets up structures to store client connection information
// client_addr: Stores client's IP address and port
// client_fd: File descriptor for client connection

    while (1) {
            client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
            // accept() is a system call that:

// Waits for incoming client connections
// Creates a new socket specifically for this client
// Returns a file descriptor for the new connection

// Parameters:

// server_fd: The main server socket that's listening for connections
// (struct sockaddr*)&client_addr: Where to store client's address info

// IP address
// Port number
// Gets typecast to generic socket address structure


// &client_len: Pointer to length of address structure

// Modified by accept() to indicate actual size of stored address

//Infinite loop to continuously accept connections
// accept(): Waits for and accepts new client connections
// server_fd: Main server socket that listens for connections
        if (client_fd < 0) {
            log_error("Accept failed");  // Implement in logging.c
            continue;
        }

        // Handle the client request through our proxy module
        if (proxy_handle_request(client_fd, config->proxy_config) < 0) {
            log_error("Proxy handle request failed");  // Implement in logging.c
        }

        close(client_fd);
    }
}

// Initialize and start the server
int server_start(server_config_t* config) {
    struct sockaddr_in server_addr;
    int opt = 1;

    // Setup signal handlers
    signal(SIGTERM, handle_signal);
    signal(SIGINT, handle_signal);

    // Create server socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        log_error("Socket creation failed");
        return -1;
    }

    // Set socket options
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        log_error("Setsockopt failed");
        return -1;
    }

    // Setup server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(config->port);

    // Bind socket
    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        log_error("Bind failed");
        return -1;
    }

    // Start listening
    if (listen(server_fd, LISTEN_BACKLOG) < 0) {
        log_error("Listen failed");
        return -1;
    }

    log_info("Server listening on port %d", config->port);

    // Fork worker processes
    for (int i = 0; i < config->worker_count; i++) {
        pid_t pid = fork();
        
        if (pid < 0) {
            log_error("Fork failed");
            return -1;
        }
        
        if (pid == 0) {  // Child process
            worker_process(config);
            exit(0);
        } else {  // Parent process
            worker_pids[i] = pid;
        }
    }

    // Parent process: wait for signals
    while (1) {
        sleep(1);
    }

    return 0;
}

// Initialize server configuration
server_config_t* server_init(void) {
    server_config_t* config = malloc(sizeof(server_config_t));
    config->port = DEFAULT_PORT;
    config->worker_count = MAX_WORKERS;
    config->proxy_config = proxy_init();
    return config;
}

// Cleanup server resources
void server_cleanup(server_config_t* config) {
    if (config) {
        if (config->proxy_config) {
            proxy_cleanup(config->proxy_config);
        }
        free(config);
    }
}