

// src/modules/proxy.c
#include "proxy.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include "../utils/logging.h"

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
    // For simplicity, we'll just use the first backend
    if (config->backend_count == 0) {
        log_error("No backend servers configured");
        return -1;
    }

    backend_server_t* backend = &config->backends[0];
    
    // Create connection to backend
    int backend_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (backend_fd < 0) {
        log_error("Failed to create backend socket");
        return -1;
    }

    // Connect to backend
    if (connect(backend_fd, (struct sockaddr*)&backend->addr, sizeof(backend->addr)) < 0) {
        log_error("Failed to connect to backend %s:%d", backend->ip, backend->port);
        close(backend_fd);
        return -1;
    }

    // Forward data between client and backend
    char buffer[BUFFER_SIZE];
    ssize_t bytes_read;

    // Forward client request to backend
    while ((bytes_read = read(client_fd, buffer, sizeof(buffer))) > 0) {
        if (write(backend_fd, buffer, bytes_read) != bytes_read) {
            log_error("Failed to forward request to backend");
            break;
        }
        
        // If we've sent the full HTTP request, break
        if (strstr(buffer, "\r\n\r\n")) break;
    }

    // Forward backend response to client
    while ((bytes_read = read(backend_fd, buffer, sizeof(buffer))) > 0) {
        if (write(client_fd, buffer, bytes_read) != bytes_read) {
            log_error("Failed to forward response to client");
            break;
        }
    }

    close(backend_fd);
    return 0;
}

void proxy_cleanup(proxy_config_t* config) {
    if (config) {
        free(config);
    }
}