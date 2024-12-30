// src/modules/proxy.h
#ifndef PROXY_H
#define PROXY_H

#include <netinet/in.h>

#define MAX_BACKENDS 10
#define MAX_IP_LENGTH 16
#define BUFFER_SIZE 8192
#define HTTP_TIMEOUT_SECONDS 30

typedef struct {
    char ip[MAX_IP_LENGTH];
    int port;
    struct sockaddr_in addr;
} backend_server_t;

typedef struct {
    backend_server_t backends[MAX_BACKENDS];
    int backend_count;
} proxy_config_t;

proxy_config_t* proxy_init(void);
int proxy_add_backend(proxy_config_t* config, const char* ip, int port);
int proxy_handle_request(int client_fd, proxy_config_t* config);
void proxy_cleanup(proxy_config_t* config);

#endif // PROXY_H