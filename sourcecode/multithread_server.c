#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdbool.h>
#include <pthread.h>

#define PORT 8085
#define BUFFER_SIZE 1024

// Struct to pass client metadata into worker thread
typedef struct {
	int client_fd;
	char client_ip[INET_ADDRSTRLEN];
	int client_port;
} client_context_t;

// Funtion prototype for thread job to handle I/O loop for a single client
void *handle_client(void *arg);


int main(void) {

	// Create the listening endpoint (IPv4, TCP stream)
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    } else {
        printf("Socket created successfully! FD: %d\n", server_fd);
    }

	// Allow immediate port reuse after server restart
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt(SO_REUSEADDR) failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

	// Configure the server's network address structure
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));

    server_addr.sin_family = AF_INET;           
    server_addr.sin_addr.s_addr = INADDR_ANY;     
    server_addr.sin_port = htons(PORT);           

	// Bind the socket to the configure IP address and port
    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("error while binding network address!");
        close(server_fd); 
        exit(EXIT_FAILURE);
    } else {
        printf("Socket successfully bound to port %d\n", PORT);
    }

	// Mark the socket as passive (ready to accept connection)
    if (listen(server_fd, 5) == -1) {
        perror("error while attempting to listen!");
        close(server_fd);
        exit(EXIT_FAILURE);     
    }

    printf("Multi-treaded server listening on port %d\n", PORT);

	// Accept loop: Greet incoming clients and dispatch to threads
    while(true){
    	struct sockaddr_in client_addr;
	socklen_t client_len = sizeof(client_addr);

	// Block execution until a new client complete the 3-way handshake
	int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
	if(client_fd < 0){
		perror("error while accepting client connection!");
		// Continue accepting other client rather than terminating the whole server
		continue;
	}

	client_context_t *ctx = malloc(sizeof(client_context_t));
	if(!ctx){
		perror("malloc failed for client context");
		close(client_fd);
		continue;
	}

	ctx->client_fd = client_fd;
	inet_ntop(AF_INET, &client_addr.sin_addr, ctx->client_ip, sizeof(ctx->client_ip));
	ctx->client_port = ntohs(client_addr.sin_port);

	printf("Accepted connection from %s:%d (Assigned FD: %d)\n", ctx->client_ip, ctx->client_port, ctx->client_fd);

	pthread_t tid;
	if(pthread_create(&tid, NULL, handle_client, (void *)ctx) != 0){
		perror("pthread_create failed");
		free(ctx);
		close(client_fd);
	}
    }
	// Main thread immediately loops back to accept() while handle_client() executes concurrently

	// Clean up listening socket
    close(server_fd);
    return 0;
}

void *handle_client(void *arg){
	// extract context allocated on heap in main function and free memory	
	client_context_t *ctx = (client_context_t *)arg;
	int client_fd = ctx->client_fd;
	char client_ip[INET_ADDRSTRLEN];
	strncpy(client_ip, ctx->client_ip, INET_ADDRSTRLEN);
	int client_port = ctx->client_port;
	free(ctx);

	// detach thread so the kernel cleans up thread resoures on exit
	// eliminates the need for the main thread to call pthread_join()
	pthread_detach(pthread_self());

	printf("[Thread %lu] Started handling %s:%d (FD: %d)\n", (unsigned long)pthread_self(), client_ip, client_port, client_fd);

	char buffer[BUFFER_SIZE];


	// Echo loop: read data from client and send back
	while(true){
		memset(buffer, 0, BUFFER_SIZE);

		ssize_t bytes_received = recv(client_fd, buffer, BUFFER_SIZE - 1, 0);

		if(bytes_received > 0){
			printf("FD %d (%s:%d)] Received %s", client_fd, client_ip, client_port, buffer);
			send(client_fd, buffer, bytes_received, 0);
		}
		else if(bytes_received == 0){
			printf("[FD %d (%s:%d)] Client disconnected\n", client_fd, client_ip, client_port);
			break;
		}
		else{
			perror("recv failed");
			break;
		}
	}
	
	close(client_fd);
	printf("[Thread %lu] Closed FD %d and exited\n", (unsigned long)pthread_self(), client_fd);
	return NULL;
}
