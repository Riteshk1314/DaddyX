#include <sys/socket.h>  
#include <netinet/in.h>  
#include <arpa/inet.h>   
#include <unistd.h>      

int main(){
        int server_fd;
        struct sockaddr_in server_addr;

        server_fd=socket(AF_INET, SOCK_STREAM,0);
        if(server_fd==-1){
                perror("sock creation failed:", 0);
                exit(EXIT_FAILURE);
        }
        memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sin_family=AF_INET;
        server_addr.sin_port=htons(80);
        server_addr.sin_addr.s_addr=htonl(INADDR_ANY);

        if(bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)<0){
                perror("Bind failed");
                close(server_fd);
                exit(EXIT_FAILURE);
        }
        if(listen(server_fd, 5) < 0) {
                perror("Listen failed");
                close(server_fd);
                exit(EXIT_FAILURE);
        }
        print("server listening on port 80...\n");
}
