// src/modules/proxy.h
#ifndef PROXY_H
#define PROXY_H

#include <netinet/in.h>

#define MAX_BACKENDS 10
#define MAX_URL_LENGTH 256
#define MAX_HEADER_SIZE 8192

typedef struct {
    char ip[16];
    int port;
    struct sockaddr_in addr;
    char* url_path;  // URL path to match (e.g., "/api/auth")
} backend_server_t;

typedef struct {
    backend_server_t backends[MAX_BACKENDS];
    int backend_count;
    cache_config_t* cache_config;
} proxy_config_t;

// Initialize proxy configuration
proxy_config_t* proxy_init(void);

// Add a backend server with URL path routing
int proxy_add_backend_with_path(proxy_config_t* config, const char* ip, int port, const char* url_path);

// Handle client request
int proxy_handle_request(int client_fd, proxy_config_t* config);

// Cleanup proxy resources
void proxy_cleanup(proxy_config_t* config);

#endif //