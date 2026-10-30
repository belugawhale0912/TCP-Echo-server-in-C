#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define IP_ADDRESS_STRING "127.0.0.1"
#define PORT 8085
#define BUFFER_SIZE 1024

int main(void) {
    // -------------------------------------------------------------------------
    // 1. Create an active socket endpoint (IPv4, TCP stream)
    // -------------------------------------------------------------------------
    // AF_INET     : IPv4 Internet protocols
    // SOCK_STREAM : Sequenced, reliable, two-way connection (TCP)
    // 0           : Kernel selects default protocol for this pair (IPPROTO_TCP)
    int client_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (client_fd == -1) {
        perror("socket failure!");
        exit(EXIT_FAILURE);
    } else {
        printf("Socket created successfully! FD: %d\n", client_fd);
    }

    // -------------------------------------------------------------------------
    // 2. Configure target server address structure
    // -------------------------------------------------------------------------
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr)); // Zero out memory/padding

    server_addr.sin_family = AF_INET;

    // Convert presentation format (IPv4 text string) to binary network byte order
    int pton_result = inet_pton(AF_INET, IP_ADDRESS_STRING, &server_addr.sin_addr);
    if (pton_result == -1) {
        perror("Invalid family address");
        close(client_fd);
        exit(EXIT_FAILURE);
    } else if (pton_result == 0) {
        fprintf(stderr, "Invalid network address string: %s\n", IP_ADDRESS_STRING);
        close(client_fd);
        exit(EXIT_FAILURE);
    }

    // Convert port from host byte order to network byte order (Big-Endian)
    server_addr.sin_port = htons(PORT);

    // -------------------------------------------------------------------------
    // 3. Initiate the TCP 3-way handshake with the listening server
    // -------------------------------------------------------------------------
    // connect() causes the kernel to dynamically assign an unused ephemeral port,
    // bind it to client_fd, and transmit the initial SYN packet.
    int connection_status = connect(client_fd, (struct sockaddr *)&server_addr, sizeof(server_addr));
    if (connection_status < 0) {
        perror("connection failed");
        close(client_fd);
        exit(EXIT_FAILURE);
    }
    printf("Connected to server at %s:%d successfully!\n", IP_ADDRESS_STRING, PORT);
    printf("Enter message (Ctrl+D or Ctrl+C to exit):\n");

    // -------------------------------------------------------------------------
    // 4. Interactive I/O loop: Read from stdin, send to server, receive echo
    // -------------------------------------------------------------------------
    char send_buffer[BUFFER_SIZE];
    char recv_buffer[BUFFER_SIZE];

    // Read lines from keyboard (stdin) until EOF (Ctrl+D)
    while (fgets(send_buffer, BUFFER_SIZE, stdin) != NULL) {
        // Transmit only the bytes entered (including trailing newline)
        ssize_t bytes_sent = send(client_fd, send_buffer, strlen(send_buffer), 0);
        if (bytes_sent < 0) {
            perror("send failed");
            break;
        }

        // Clear receive buffer before reading response
        memset(recv_buffer, 0, BUFFER_SIZE);

        // Read up to BUFFER_SIZE - 1 bytes to guarantee null-termination for %s
        ssize_t bytes_received = recv(client_fd, recv_buffer, BUFFER_SIZE - 1, 0);

        if (bytes_received > 0) {
            // Data received from server
            printf("Server echo: %s", recv_buffer);
        } else if (bytes_received == 0) {
            // Server initiated active close (sent TCP FIN)
            printf("Server closed the connection.\n");
            break;
        } else {
            // System call error on recv()
            perror("recv failed");
            break;
        }
    }

    // -------------------------------------------------------------------------
    // 5. Cleanup and teardown
    // -------------------------------------------------------------------------
    // Initiates client-side active close by sending a TCP FIN packet
    close(client_fd);
    return 0;
}
