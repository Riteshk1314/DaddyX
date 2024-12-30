// src/modules/proxy.c
#include "proxy.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include "../utils/logging.h"

#define BUFFER_SIZE 8192

// HTTP request parser
typedef struct {
    char method[16];
    char path[MAX_URL_LENGTH];
    char headers[MAX_HEADER_SIZE];
    char* body;
    size_t body_length;
} http_request_t;

// Parse HTTP request
static int parse_http_request(const char* raw_request, size_t request_len, http_request_t* request) {
    // Initialize request structure
    memset(request, 0, sizeof(http_request_t));
    
    // Parse first line (METHOD PATH HTTP/1.1)
    char* line_end = strstr(raw_request, "\r\n");
    if (!line_end) return -1;
    
    // Parse method
    const char* space = strchr(raw_request, ' ');
    if (!space) return -1;
    size_t method_len = space - raw_request;
    strncpy(request->method, raw_request, method_len < 15 ? method_len : 15);
    
    // Parse path
    const char* path_start = space + 1;
    space = strchr(path_start, ' ');
    if (!space) return -1;
    size_t path_len = space - path_start;
    strncpy(request->path, path_start, path_len < MAX_URL_LENGTH ? path_len : MAX_URL_LENGTH - 1);
    
    return 0;
}

// Find matching backend for request
static backend_server_t* find_backend(proxy_config_t* config, const char* path) {
    for (int i = 0; i < config->backend_count; i++) {
        if (config->backends[i].url_path && 
            strncmp(path, config->backends[i].url_path, strlen(config->backends[i].url_path)) == 0) {
            return &config->backends[i];
        }
    }
    
    // Return first backend if no match (default)
    return config->backend_count > 0 ? &config->backends[0] : NULL;
}

int proxy_add_backend_with_path(proxy_config_t* config, const char* ip, int port, const char* url_path) {
    if (config->backend_count >= MAX_BACKENDS) {
        log_error("Maximum number of backends reached");
        return -1;
    }

    backend_server_t* backend = &config->backends[config->backend_count];
    
    // Copy IP and port
    strncpy(backend->ip, ip, sizeof(backend->ip) - 1);
    backend->port = port;

    // Copy URL path if provided
    if (url_path) {
        backend->url_path = strdup(url_path);
    } else {
        backend->url_path = NULL;
    }

    // Setup backend address
    memset(&backend->addr, 0, sizeof(backend->addr));
    backend->addr.sin_family = AF_INET;
    backend->addr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip, &backend->addr.sin_addr) <= 0) {
        log_error("Invalid backend IP address");
        return -1;
    }

    config->backend_count++;
    log_info("Added backend server %s:%d for path %s", ip, port, url_path ? url_path : "default");
    return 0;
}

int proxy_handle_request(int client_fd, proxy_config_t* config) {
    char buffer[BUFFER_SIZE];
    ssize_t bytes_read;
    http_request_t request;

    // Read the request
    bytes_read = read(client_fd, buffer, sizeof(buffer) - 1);
    if (bytes_read <= 0) return -1;
    buffer[bytes_read] = '\0';

    // Parse the request
    if (parse_http_request(buffer, bytes_read, &request) < 0) {
        log_error("Failed to parse HTTP request");
        return -1;
    }

    // Find appropriate backend
    backend_server_t* backend = find_backend(config, request.path);
    if (!backend) {
        log_error("No backend available for path: %s", request.path);
        return -1;
    }

    log_info("Forwarding request to backend %s:%d for path %s", 
             backend->ip, backend->port, request.path);

    // Connect to backend
    int backend_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (backend_fd < 0) {
        log_error("Failed to create backend socket");
        return -1;
    }

    if (connect(backend_fd, (struct sockaddr*)&backend->addr, sizeof(backend->addr)) < 0) {
        log_error("Failed to connect to backend %s:%d", backend->ip, backend->port);
        close(backend_fd);
        return -1;
    }

    // Forward the original request to backend
    if (write(backend_fd, buffer, bytes_read) != bytes_read) {
        log_error("Failed to forward request to backend");
        close(backend_fd);
        return -1;
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