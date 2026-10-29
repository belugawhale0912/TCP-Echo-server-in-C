#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdbool.h>

#define PORT 8085
#define BUFFER_SIZE 1024

int main(void) {
    // -------------------------------------------------------------
    // 1. Create the listening endpoint (IPv4, TCP stream)
    // -------------------------------------------------------------
    // AF_INET     : IPv4 Internet protocols
    // SOCK_STREAM : Sequenced, reliable, two-way connection (TCP)
    // 0           : Automatically choose default protocol (IPPROTO_TCP)
    int ipv4_tcp = socket(AF_INET, SOCK_STREAM, 0);
    if (ipv4_tcp == -1) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    } else {
        printf("Socket created successfully! FD: %d\n", ipv4_tcp);
    }

    // -------------------------------------------------------------
    // 2. Configure the server's network address structure
    // -------------------------------------------------------------
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr)); // Zero out memory/padding

    server_addr.sin_family = AF_INET;             // IPv4 address family
    server_addr.sin_addr.s_addr = INADDR_ANY;     // Bind to all local network interfaces (0.0.0.0)
    server_addr.sin_port = htons(PORT);           // Convert host byte order to network byte order (Big-Endian)

    // -------------------------------------------------------------
    // 3. Bind the socket to the configured IP address and port
    // -------------------------------------------------------------
    // Cast (struct sockaddr_in *) to the generic (struct sockaddr *) required by POSIX
    if (bind(ipv4_tcp, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("error while binding network address!");
        close(ipv4_tcp); // Release socket descriptor before exiting
        exit(EXIT_FAILURE);
    } else {
        printf("Socket successfully bound to port\n");
    }

    // -------------------------------------------------------------
    // 4. Mark the socket as passive (ready to accept incoming connections)
    // -------------------------------------------------------------
    // Backlog (5): Maximum length of pending connections queue in kernel
    if (listen(ipv4_tcp, 5) == -1) {
        perror("error while attempting to listen!");
        close(ipv4_tcp);
        exit(EXIT_FAILURE);     
    }

    // -------------------------------------------------------------
    // 5. Wait for and extract an incoming connection (accept)
    // -------------------------------------------------------------
    struct sockaddr_in client_addr;
    // client_len is a value-result argument: input buffer size, kernel overwrites with written size
    socklen_t client_len = sizeof(client_addr);

    printf("Waiting for incoming connections on port 8085\n");

    // Blocks execution until a client completes the TCP 3-way handshake.
    // Returns a brand-new descriptor (client_fd) dedicated to this connection;
    // ipv4_tcp remains open and listening for other connections.
    int client_fd = accept(ipv4_tcp, (struct sockaddr *)&client_addr, &client_len);
    if (client_fd < 0) {
        perror("error while accepting client connection!");
        close(ipv4_tcp);
        exit(EXIT_FAILURE);     
    } else {
        // Convert binary network address to human-readable IPv4 string
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
        // ntohs(): Network to Host Short (converts Big-Endian port back to host format)
        printf("Accepted connection from %s:%d\n", client_ip, ntohs(client_addr.sin_port));
    }

    // -------------------------------------------------------------
    // 6. Echo loop: Read data from client and write it straight back
    // -------------------------------------------------------------
    char buffer[BUFFER_SIZE];

    while (true) {
        memset(buffer, 0, BUFFER_SIZE);

        // Read up to BUFFER_SIZE - 1 bytes to guarantee null-termination for %s
        ssize_t bytes_received = recv(client_fd, buffer, BUFFER_SIZE - 1, 0);
        
        if (bytes_received > 0) {
            // Data received successfully
            printf("Received: %s", buffer);
            // Echo back only the exact number of bytes received (avoids transmitting trailing garbage)
            send(client_fd, buffer, bytes_received, 0);
        } else if (bytes_received == 0) {
            // Orderly shutdown: peer called close() / sent TCP FIN
            printf("Client disconnected\n");
            break;
        } else {
            // recv returned -1 (system call error)
            perror("recv failed\n");
            break;              
        }
    }

    // -------------------------------------------------------------
    // 7. Cleanup and teardown
    // -------------------------------------------------------------
    // Initiates active close on both sockets (sends TCP FIN packets)
    close(client_fd);
    close(ipv4_tcp);
    return 0;
}