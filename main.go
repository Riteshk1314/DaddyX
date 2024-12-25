#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#define BUFFER_SIZE 4096
#define BACKEND_HOST "127.0.0.1"
#define BACKEND_PORT 8080

int main() {
    int server_fd, client_fd, backend_fd;
    struct sockaddr_in server_addr, client_addr, backend_addr;
    char buffer[BUFFER_SIZE];
    socklen_t addr_len = sizeof(client_addr);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(80);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 10) < 0) {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }

    printf("Reverse proxy listening on port 80\n");

    while (1) {
        client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &addr_len);
        if (client_fd < 0) {
            perror("Client accept failed");
            continue;
        }

        memset(buffer, 0, BUFFER_SIZE);
        recv(client_fd, buffer, BUFFER_SIZE, 0);

        backend_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (backend_fd < 0) {
            perror("Backend socket creation failed");
            close(client_fd);
            continue;
        }

        backend_addr.sin_family = AF_INET;
        backend_addr.sin_port = htons(BACKEND_PORT);
        inet_pton(AF_INET, BACKEND_HOST, &backend_addr.sin_addr);

        if (connect(backend_fd, (struct sockaddr*)&backend_addr, sizeof(backend_addr)) < 0) {
            perror("Connect to backend failed");
            close(client_fd);
            close(backend_fd);
            continue;
        }

        send(backend_fd, buffer, strlen(buffer), 0);

        memset(buffer, 0, BUFFER_SIZE);
        recv(backend_fd, buffer, BUFFER_SIZE, 0);

        send(client_fd, buffer, strlen(buffer), 0);

        close(client_fd);
        close(backend_fd);
    }

    close(server_fd);
    return 0;
}

