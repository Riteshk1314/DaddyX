// src/modules/proxy.h
#ifndef PROXY_H
#define PROXY_H

#include <netinet/in.h>

#define MAX_BACKENDS 10

typedef struct {
    char ip[16];         // Backend IP address
    int port;           // Backend port
    struct sockaddr_in addr;  // Backend address structure
} backend_server_t;

typedef struct {
    backend_server_t backends[MAX_BACKENDS];
    int backend_count;
    // Add more proxy configuration options here
} proxy_config_t;

// Initialize proxy configuration
proxy_config_t* proxy_init(void);

// Add a backend server
int proxy_add_backend(proxy_config_t* config, const char* ip, int port);

// Handle client request
int proxy_handle_request(int client_fd, proxy_config_t* config);

// Cleanup proxy resources
void proxy_cleanup(proxy_config_t* config);

#endif // PROXY_H