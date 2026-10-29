#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define PORT 8085

int main(){

	int ipv4_tcp = socket(AF_INET, SOCK_STREAM, 0);
	if(ipv4_tcp == -1){
		perror("socket failed");
		exit(EXIT_FAILURE);
	}
	else{
		printf("Socket created successfully! FD: %d\n", ipv4_tcp);
	}

	struct sockaddr_in server_addr;
	memset(&server_addr, 0, sizeof(server_addr));

	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = INADDR_ANY;
	server_addr.sin_port = htons(PORT);

	if(bind(ipv4_tcp, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1){
		perror("error while binding network address!");
		close(ipv4_tcp);
		exit(EXIT_FAILURE);
	}
	else{
		printf("Socket successfully bound to port\n");
	}

	close(ipv4_tcp);
	return 0;
}

