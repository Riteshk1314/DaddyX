// src/core/server.h
#ifndef SERVER_H
#define SERVER_H

#include "../modules/proxy.h"

// Server configuration structure
typedef struct {
    int port;                  // Server listening port
    int worker_count;          // Number of worker processes
    proxy_config_t* proxy_config;  // Proxy configuration
} server_config_t;

// Initialize server configuration
// Returns: Newly allocated server configuration or NULL on failure
server_config_t* server_init(void);

// Start the server with given configuration
// Returns: 0 on success, -1 on failure
int server_start(server_config_t* config);

// Clean up server resources
// config: Server configuration to clean up
void server_cleanup(server_config_t* config);

#endif // SERVER_H