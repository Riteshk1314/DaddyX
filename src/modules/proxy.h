#ifndef PROXY_H
#define PROXY_H

#include <netinet/in.h>
#include "cache.h"  // Add this include

#define MAX_BACKENDS 10
#define MAX_URL_LENGTH 2048
#define MAX_HEADER_SIZE 8192

typedef struct {
    char ip[16];
    int port;
    char* url_path;
    struct sockaddr_in addr;
} backend_server_t;

typedef struct {
    backend_server_t backends[MAX_BACKENDS];
    int backend_count;
    cache_config_t* cache_config;  // Now cache_config_t is defined
} proxy_config_t;

// Function prototypes
int proxy_add_backend_with_path(proxy_config_t* config, const char* ip, int port, const char* url_path);
int proxy_handle_request(int client_fd, proxy_config_t* config);

#endif // PROXY_H