#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#define BUFFER_SIZE 4096
#define BACKEND_HOST "15.197.148.33"  // Backend server address
#define BACKEND_PORT 8081         // Backend server port
#define PROXY_PORT 80             // Reverse proxy port

void handle_client(int client_fd);

int main() {
    int server_fd, client_fd, backend_fd;
    struct sockaddr_in server_addr, client_addr, backend_addr;
    socklen_t addr_len = sizeof(client_addr);
    char buffer[BUFFER_SIZE];

    // Create server socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Setup server address for proxy (listening on port 80)
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PROXY_PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;  // Listen on all interfaces

    // Bind the socket to the address
    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }

    // Start listening for incoming connections
    if (listen(server_fd, 10) < 0) {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }

    printf("Reverse proxy listening on port %d\n", PROXY_PORT);

    while (1) {
        // Accept incoming client connections
        client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &addr_len);
        if (client_fd < 0) {
            perror("Client accept failed");
            continue;
        }

        // Handle the client request in a separate function
        handle_client(client_fd);
    }

    // Close the server socket
    close(server_fd);
    return 0;
}

void handle_client(int client_fd) {
    int backend_fd;
    struct sockaddr_in backend_addr;
    char buffer[BUFFER_SIZE];
    ssize_t client_recv_size, backend_recv_size;

    // Receive data from the client
    memset(buffer, 0, BUFFER_SIZE);
    client_recv_size = recv(client_fd, buffer, BUFFER_SIZE, 0);
    if (client_recv_size <= 0) {
        perror("Client read failed or no data received");
        close(client_fd);
        return;
    }

    // Create socket to connect to the backend server
    backend_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (backend_fd < 0) {
        perror("Backend socket creation failed");
        close(client_fd);
        return;
    }

    // Setup backend server address
    memset(&backend_addr, 0, sizeof(backend_addr));
    backend_addr.sin_family = AF_INET;
    backend_addr.sin_port = htons(BACKEND_PORT);
    inet_pton(AF_INET, BACKEND_HOST, &backend_addr.sin_addr);

    // Connect to the backend server
    if (connect(backend_fd, (struct sockaddr*)&backend_addr, sizeof(backend_addr)) < 0) {
        perror("Connect to backend failed");
        close(client_fd);
        close(backend_fd);
        return;
    }

    // Send the client's request to the backend
    if (send(backend_fd, buffer, client_recv_size, 0) < 0) {
        perror("Send to backend failed");
        close(client_fd);
        close(backend_fd);
        return;
    }

    // Receive the backend's response
    memset(buffer, 0, BUFFER_SIZE);
    backend_recv_size = recv(backend_fd, buffer, BUFFER_SIZE, 0);
    if (backend_recv_size <= 0) {
        perror("Backend read failed or no data received");
        close(client_fd);
        close(backend_fd);
        return;
    }

    // Send the backend's response to the client
    if (send(client_fd, buffer, backend_recv_size, 0) < 0) {
        perror("Send to client failed");
        close(client_fd);
        close(backend_fd);
        return;
    }

    // Close the connections
    close(client_fd);
    close(backend_fd);
}

