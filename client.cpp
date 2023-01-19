#include <arpa/inet.h>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdio.h>
#include <string.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>
using namespace std;
#define PORT 8080

int main(int argc, char const* argv[]) {
	int sock = 0, client_fd;
	struct sockaddr_in serv_addr;
	if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		printf("\n Socket creation error \n");
		return -1;
	}

	serv_addr.sin_family = AF_INET;
	serv_addr.sin_port = htons(PORT);

	// Convert IPv4 and IPv6 addresses from text to binary
	if (inet_pton(AF_INET, "127.0.1.1", &serv_addr.sin_addr)
		<= 0) {
		printf(
			"\nInvalid address/ Address not supported \n");
		return -1;
	}

	if ((client_fd
		= connect(sock, (struct sockaddr*)&serv_addr,
				sizeof(serv_addr)))
		< 0) {
		printf("\nConnection Failed \n");
		return -1;
	}

	cout << "create <path>\n";
	cout << "open <path> <mode>\n";
	cout << "close <path>\n";
	cout << "delete <path>\n";
	cout << "chdir <path>\n";
	cout << "write_to_file <path> <content>\n";
	cout << "read_from_file <path>\n";
	cout << "truncate_file <path> <size>\n";
	cout << "show_memory_map\n";
	cout << "exit\n\n"; 

	string username;
	cout << "Enter username: ";
	getline(cin, username);

	const char *cLine = username.c_str();
	send(sock, cLine, strlen(cLine), 0);

	char result[1024] = {0};
	int valread = read(sock, result, 1024);
	cout << result << endl;
	
	string line;
	while (1) {
		cout << "Enter what you wanna do: " << endl;
		getline(cin, line);
		cout << line << endl;
		
		// send to server
		const char *cLine = line.c_str();
		send(sock, cLine, strlen(cLine), 0);
		
		// receive from server
		char result[1024] = {0};
        int valread = read(sock, result, 1024);
		cout << result << endl;

		if (line == "exit") {
			cout << "Exiting..." << endl;
			break;
		}
	} 

	// closing the connected socket
	close(client_fd);
	return 0;
}