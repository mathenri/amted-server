/*
 * 550server.cpp
 */

#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <poll.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <vector>

#include "threadpool.hpp"

#define BACKLOG 10

/*
 * A struct containing data for a specific client.
 */
struct client {
	// socket the client communicates over
	int socket_fd;

	// buffer to put input data (filename) from client in
	char input_data[512];

	// a buffer to which the file content is read
	char *file_content;

	// boolean specifying if the file content has completely been placed in the
	// file content buffer
	bool file_content_ready;

};

/*
 * Reads a file and put its content in a buffer. Sets the read_file_complete
 * flag to true when read is complete. Returns true if the read was successful.
 */
void read_file(void *data) {

	// extract parameters
	void **args = (void **) data;
	char *filename = (char *) args[0];
	char **file_content_pointer = (char **) args[1];
	bool *read_file_complete_pointer = (bool *) args[2];

	FILE *file_pointer;
	long file_size;

	// open file stream
	file_pointer = fopen(filename, "rb");
	if (!file_pointer) {
		perror("fopen");
		return;
	}

	// find size of file ...
	// ... put file pointer at end of file
	fseek(file_pointer, 0L, SEEK_END);

	// ... get file pointer value
	file_size = ftell(file_pointer);

	// ... rewind file pointer to beginning
	rewind(file_pointer);

	// allocate buffer memory
	*file_content_pointer = (char *) calloc(1, file_size + 1);
	if (!(*file_content_pointer)) {
		fclose(file_pointer);
		perror("calloc");
		return;
	}

	// copy file into buffer
	if (1 != fread(*file_content_pointer, file_size, 1, file_pointer)) {
		fclose(file_pointer);
		free(*file_content_pointer);
		perror("fread");
		return;
	}

	// file content is copied into buffer, set read_file_complete flag to true
	*read_file_complete_pointer = true;

	fclose(file_pointer);
}

/**
 * Set a socket to non-blocking mode.
 */
void set_nonblocking_mode(int fd) {
	int flags;

	// get flags
	flags = fcntl(fd, F_GETFL);

	// add non-blocking flag
	fcntl(fd, F_SETFL, flags);
}

/*
 * Dispatches a thread that reads the file requested by a client
 */
void dispatch_read_file(char* filename, struct threadpool *tp,
		char **file_content_pointer, bool* file_content_ready) {

	printf("server: filename received from client: %s\n", filename);

	// create void pointers to user arguments
	void *args[3];
	args[0] = filename;
	args[1] = file_content_pointer;
	args[2] = file_content_ready;

	// dispatches a thread
	dispatch(tp, read_file, args);
}

/*
 * Extracts ip and port given by the user as input arguments.
 */
void extract_arguments(int argc, char **argv, char **ip_pointer,
		char **port_pointer) {
	if (argc == 3) {
		*ip_pointer = argv[1];
		*port_pointer = argv[2];
	} else {
		perror("Exiting: Wrong number of arguments! \nUsage: ./server550 <ip> "
				"<port>");
		exit(1);
	}
}

/*
 * Sets up a listening server socket on a given ip and port
 */
void setup_server_socket(char *ip, char *port, int *server_socket_fd) {
	struct addrinfo hints;
	struct addrinfo *server_info;

	// get an addrinfo object with info on the address we want to bind to
	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	if ((getaddrinfo(ip, port, &hints, &server_info)) != 0) {
		perror("getaddrinfo");
		exit(1);
	}

	// create the server socket
	if ((*server_socket_fd = socket(server_info->ai_family,
			server_info->ai_socktype, server_info->ai_protocol)) == -1) {
		perror("server: socket");
		exit(1);
	}

	// set server socket options
	int yes = 1;
	if (setsockopt(*server_socket_fd, SOL_SOCKET, SO_REUSEADDR, &yes,
			sizeof(int)) == -1) {
		perror("setsockopt");
		exit(1);
	}

	// bind the server socket to the ip and port provided by the user
	if (bind(*server_socket_fd, server_info->ai_addr, server_info->ai_addrlen)
			== -1) {
//		close(*server_socket_fd);
		perror("server: bind");
		exit(1);
	}

	freeaddrinfo(server_info);

	// set the server socket to listen for incoming client connections
	if (listen(*server_socket_fd, BACKLOG) == -1) {
		perror("listen");
		exit(1);
	}

	/* set server socket to non-blocking mode so we can utilize asynchronous
	 network I/O */
	set_nonblocking_mode(*server_socket_fd);

	printf("server up and listening for client connections ...\n");
}

/*
 * setup a pollfd struct for the poll() function containing all the sockets
 * currently active at the server
 */
void setup_poll_fds(struct pollfd **poll_fds_pointer,
		int server_socket_fd, std::vector<struct client*> *clients) {

	*poll_fds_pointer = (struct pollfd *) calloc(clients->size() + 1,
			sizeof(struct pollfd));

	// ... setup server socket poll file descriptor
	(*poll_fds_pointer)[0].fd = server_socket_fd;
	(*poll_fds_pointer)[0].events = POLLIN;

	// ... setup client socket poll file descriptors
	for (int j = 0; j < clients->size(); j++) {
		(*poll_fds_pointer)[j + 1].fd = (*clients)[j]->socket_fd;
		(*poll_fds_pointer)[j + 1].events = POLLIN | POLLOUT;
	}
}

/*
 * sets up a new client socket
 */
int setup_new_client(int server_socket_fd, std::vector<struct client *> *clients) {
	struct client *cli;

	// accept new client socket
	int client_socket_fd;
	struct sockaddr_in client_addr;
	socklen_t client_len = sizeof(client_addr);

	client_socket_fd = accept(server_socket_fd,
			(struct sockaddr *) &client_addr, &client_len);
	if (client_socket_fd == -1) {
		perror("accept");
		return -1;
	}

	set_nonblocking_mode(client_socket_fd);

	// create new client struct object for the client ...
	cli = (struct client*) calloc(1, sizeof(*cli));
	if (cli == NULL) {
		perror("malloc");
	}

	cli->file_content_ready = false;
	cli->socket_fd = client_socket_fd;
	clients->push_back(cli);

	return 0;
}

/*
 * read input from a client socket
 */
void read_client_input(struct client *cli) {
	// get input from client
	int recieved_bytes = recv(cli->socket_fd, cli->input_data,
			sizeof(cli->input_data), 0);

	if (recieved_bytes == -1) {
		perror("recv");
	}

	cli->input_data[recieved_bytes - 2] = '\0';
}

/*
 * write file content to a client socket
 */
void write_file_to_client(struct client *cli) {
	if (send(cli->socket_fd, cli->file_content, strlen(cli->file_content), 0)
			== -1) {
		perror("send");
	}

	free(cli->file_content);
	cli->file_content_ready = false;

}

int main(int argc, char **argv) {

	int server_socket_fd;
	char *ip;
	char *port;
	bool new_client_socket = false;
	std::vector<struct client *> clients;

	struct threadpool *tp = init_threadpool(5);

	// extract user arguments
	extract_arguments(argc, argv, &ip, &port);

	// set up listening server socket
	setup_server_socket(ip, port, &server_socket_fd);

	// set up server socket file descriptor for poll()
	struct pollfd *poll_fds;
	setup_poll_fds(&poll_fds, server_socket_fd, &clients);

	// start event loop
	while (1) {

		// if there is a new client connected
		if (new_client_socket) {

			// update poll file descriptors
			free(poll_fds);
			setup_poll_fds(&poll_fds, server_socket_fd, &clients);
			new_client_socket = false;
		}

		// wait for events on all the sockets
		if (poll(poll_fds, clients.size() + 1, -1) == -1) {
			perror("poll");
		} else {

			// if a new client is connecting to the server socket ...
			if (poll_fds[0].revents & POLLIN) {
				new_client_socket = true;

				// setup new client
				if (setup_new_client(server_socket_fd, &clients)
						== -1) {
					perror("couldn't add client, try again");
					continue;
				}

				// ... go back and update poll file descriptors
				continue;
			}

			// for all connected clients ...
			for (int k = 0; k < clients.size(); k++) {
				struct client *cli = clients[k];

				// any input from client on this socket?
				if (poll_fds[k + 1].revents & POLLIN) {

					// get input from client
					read_client_input(cli);

					// dispatch thread that starts reading the file
					dispatch_read_file(cli->input_data, tp,
							&(cli->file_content), &(cli->file_content_ready));
				}

				/* any client sockets ready to read, and file content for them
				 is ready? */
				if ((poll_fds[k + 1].revents & POLLOUT)
						&& cli->file_content_ready) {

					// write file content to client
					write_file_to_client(cli);

					// close client connection
					close(cli->socket_fd);
					free(cli);
					clients.erase(clients.begin() + k);
				}
			}
		}
	}
	return 0;
}


