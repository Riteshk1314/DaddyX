// src/main.c
#include <stdio.h>
#include <stdlib.h>
#include "../src/core/server.h"
#include "../src/modules/proxy.h"
#include "../src/utils/logging.h" 
int main(int argc, char *argv[]) {
    // Initialize server configuration
    server_config_t* server_config = server_init();
    if (!server_config) {
        fprintf(stderr, "Failed to initialize server configuration\n");
        return EXIT_FAILURE;
    }

    // Add backend for API requests
    proxy_add_backend_with_path(server_config->proxy_config, "93.127.172.77", 5000, "/api/auth");
    
    // Add default backend (optional)
    proxy_add_backend_with_path(server_config->proxy_config, "15.197.148.33", 8081, NULL);

    server_config->port = 8080;
    server_config->worker_count = 8;

    // Start the server
    int result = server_start(server_config);
    
    server_cleanup(server_config);
    
    return result == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}