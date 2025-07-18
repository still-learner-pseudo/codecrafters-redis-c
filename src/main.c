#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <sys/time.h>

#define MAX_CLIENTS 10

// currently the implementation uses linked list, which is not efficient, i want to improve it by using hash table.
struct key_value {
    long long timestamp;
    int time_to_live;
    char* key;
    char* value;
    struct key_value* next;
};

typedef struct key_value key_value_t;
key_value_t* head = NULL;

void handle_input(char* buffer);
void parse_input(char* buffer, char* command, char** arguments, int* argument_count, int* length);
void set_key_value(char* key, char* value, char* buffer, char* option, int time_to_live);
void get_key_value(char* key, char* buffer);
key_value_t* create_node(char* key, char* value, int time_to_live);
void delete_key_value(char* key);
void free_list();

int main() {
	// Disable output buffering
	setbuf(stdout, NULL);
	setbuf(stderr, NULL);

	// You can use print statements as follows for debugging, they'll be visible when running tests.
	printf("Logs from your program will appear here!\n");

	// Uncomment this block to pass the first stage
	//
	int server_fd;
	socklen_t client_addr_len;
	struct sockaddr_in client_addr;

	server_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (server_fd == -1) {
		printf("Socket creation failed: %s...\n", strerror(errno));
		return 1;
	}

	// Since the tester restarts your program quite often, setting SO_REUSEADDR
	// ensures that we don't run into 'Address already in use' errors
	int reuse = 1;
	if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
		printf("SO_REUSEADDR failed: %s \n", strerror(errno));
		return 1;
	}

	struct sockaddr_in serv_addr = { .sin_family = AF_INET ,
									 .sin_port = htons(6379),
									 .sin_addr = { htonl(INADDR_ANY) },
									};

	if (bind(server_fd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) != 0) {
		printf("Bind failed: %s \n", strerror(errno));
		return 1;
	}

	int connection_backlog = 5;
	if (listen(server_fd, connection_backlog) != 0) {
		printf("Listen failed: %s \n", strerror(errno));
		return 1;
	}

	printf("Waiting for a client to connect...\n");
	client_addr_len = sizeof(client_addr);

	fd_set read_fds;
	int max_fd = server_fd;
	int client_fds[MAX_CLIENTS];
	int num_clients = 0;

	while(1) {
        FD_ZERO(&read_fds); // set all bits to zero
        FD_SET(server_fd, &read_fds); // set server_fd bit to one

        for(int i = 0; i < num_clients; i++) {
            FD_SET(client_fds[i], &read_fds); // set client_fds[i] bit to one
            if(client_fds[i] > max_fd)
                max_fd = client_fds[i];
        }

        // using select to handle multiple clients
        int ready = select(max_fd + 1, &read_fds, NULL, NULL, NULL);
        // future work: use epoll instead of select because it is more efficient and scalable
        if(ready == -1) {
            perror("select");
            exit(EXIT_FAILURE);
        }

        if(FD_ISSET(server_fd, &read_fds)) {
            int new_client_fd = accept(server_fd, (struct sockaddr*) &client_addr, &client_addr_len);
            if(new_client_fd > 0) {
                if(num_clients < MAX_CLIENTS) {
                    printf("Client %d Connected\n", num_clients);
                    client_fds[num_clients++] = new_client_fd;
                } else {
                    printf("Maximum number of clients reached\n");
                    close(new_client_fd);
                }
            }
        }

        for(int i = 0; i < num_clients; i++) {
            if(FD_ISSET(client_fds[i], &read_fds)) {
                char buffer[1024];
                ssize_t bytes_read = read(client_fds[i], buffer, sizeof(buffer));
                if(bytes_read > 0) {
                    buffer[bytes_read] = '\0';
                    printf("Client %d: %s\n", i, buffer);
                    handle_input(buffer);
                    write(client_fds[i], buffer, strlen(buffer));
                } else if(bytes_read == 0) {
                    printf("Client %d disconnected\n", i);
                    close(client_fds[i]);
                    for (int j = i; j < num_clients - 1; j++) {
                        client_fds[j] = client_fds[j + 1];
                    }
                    num_clients--;
                    i--;
                } else {
                    printf("Error reading from client %d: %s\n", i, strerror(errno));
                }
            }
        }
	}

	close(server_fd);
	free_list();

	return 0;
}

void handle_input(char* buffer) {
    char command[100];
    char* arguments[100];
    int arguments_count = 0;
    int length = 0;
    parse_input(buffer, command, arguments, &arguments_count, &length);

    if (strcmp(command, "PING") == 0) {
            strcpy(buffer, "+PONG\r\n"); // this is simple without any formatting.
    } else if (strcmp(command, "ECHO") == 0) {
        if (arguments_count < 2) { // if there are less than 2 arguments that means only the command ECHO was sent
            strcpy(buffer, "-ERR wrong number of arguments for 'echo' command\r\n");
        } else {
            snprintf(buffer, 1024, "$%ld\r\n%s\r\n", strlen(arguments[1]), arguments[1]); // we need of the form $<length>\r\n<value>\r\n
        }
    } else if (strcmp(command, "SET") == 0) {
        if (arguments_count < 3) { // if there are less than 3 arguments that means only the command SET and key was sent, value is missing
            strcpy(buffer, "-ERR wrong number of arguments for 'set' command\r\n");
        } else {
            if(arguments_count == 5)
                set_key_value(arguments[1], arguments[2], buffer, arguments[3], atoi(arguments[4]));
            else
                set_key_value(arguments[1], arguments[2], buffer, NULL, -1);
        }
    } else if (strcmp(command, "GET") == 0) {
        if (arguments_count < 2) { // if there are less than 2 arguments that means only the command GET was sent, key is missing
            strcpy(buffer, "-ERR wrong number of arguments for 'get' command\r\n");
        } else {
            get_key_value(arguments[1], buffer);
        }
    }

    for(int i = 0; i < arguments_count; i++) {
        free(arguments[i]);
    }
}
// this function sets the key value pair in the linked list, for now ignoring the options value
void set_key_value(char* key, char* value, char* buffer, char* options, int time_to_live) {
    if(head == NULL) {
        key_value_t* new_node = create_node(key, value, time_to_live);
        if(new_node == NULL) {
            strcpy(buffer, "-ERR out of memory\r\n");
            return;
        }
        head = new_node;
    } else {
        key_value_t* current = head;
        while(current != NULL) {
            if(strcmp(current->key, key) == 0) {
                free(current->value);
                current->value = strdup(value);
                if(current->value == NULL) {
                    strcpy(buffer, "$-1\r\n");
                    return;
                }
                strcpy(buffer, "+OK\r\n");
                return;
            }
            current = current->next;
        }
        key_value_t* new_node = create_node(key, value, time_to_live);
        if(new_node == NULL) {
            strcpy(buffer, "$-1\r\n");
            return;
        }
        new_node->next = head;
        head = new_node;
    }
    strcpy(buffer, "+OK\r\n");
}

// we use this function to create a new node in the linked list
key_value_t* create_node(char* key, char* value, int time_to_live) {
    key_value_t* new_node = malloc(sizeof(key_value_t));
    if(new_node == NULL) {
        return NULL;
    }
    struct timeval tv;
    gettimeofday(&tv, NULL);
    new_node->timestamp = tv.tv_sec * 1000LL + tv.tv_usec / 1000LL;
    new_node->time_to_live = time_to_live;
    new_node->key = strdup(key);
    new_node->value = strdup(value);
    new_node->next = NULL;
    return new_node;
}

// we use this function to get the key value pair from the linked list, if it is expired then delete it and send -1.
void get_key_value(char* key, char* buffer) {
    key_value_t* current = head;
    struct timeval now;
    while(current != NULL) {
        if(strcmp(current->key, key) == 0) {
            if(current->time_to_live > 0) {
                gettimeofday(&now, NULL);
                long long elapsed = now.tv_sec * 1000LL + now.tv_usec / 1000LL - current->timestamp;
                if(elapsed >= current->time_to_live) {
                    delete_key_value(key);
                    strcpy(buffer, "$-1\r\n");
                    return;
                }
            }
            snprintf(buffer, 1024, "$%ld\r\n%s\r\n", strlen(current->value), current->value);
            return;
        }
        current = current->next;
    }
    strcpy(buffer, "$-1\r\n");
}

// we use this function to delete the key value pair from the linked list.
void delete_key_value(char* key) {
    key_value_t* current = head;
    key_value_t* prev = NULL;
    while(current != NULL) {
        if(strcmp(current->key, key) == 0) {
            if(prev == NULL) {
                head = current->next;
            } else {
                prev->next = current->next;
            }
            free(current->key);
            free(current->value);
            free(current);
            return;
        }
        key_value_t* next = current->next;
        free(current->key);
        free(current->value);
        free(current);
        current = next;
    }
}

// we use this function to free the entire linked list.
void free_list() {
    key_value_t* current = head;
    while(current != NULL) {
        key_value_t* next = current->next;
        free(current->key);
        free(current->value);
        free(current);
        current = next;
    }
    head = NULL;
}

// please refer here for RESP protocol specification:
// // https://redis.io/docs/latest/develop/reference/protocol-spec/

void parse_input(char* buffer, char* command, char** arguments, int* argument_count, int* length) {
    int pos = 0;
    // according to RESP, initially we get array i.e * for anything other than PING

    if(buffer[pos] != '*') return;
    pos++;

    int num_elements = atoi(&buffer[pos]);
    while(buffer[pos] != '\r' && buffer[pos] != '\n') pos++; // we are skipping anything that is not a new line or carriage return
    pos += 2; // now we are also skipping the new line or carriage return

    for(int i = 0; i < num_elements; i++) {
        if(buffer[pos] != '$') return; // the length of anything starts with $.
        pos++; // skip the $ symbol

        int bulk_len = atoi(&buffer[pos]);
        while(buffer[pos] != '\r' && buffer[pos] != '\n') pos++; // we are skipping anything that is not a new line or carriage return
        pos += 2; // now we are also skipping the new line or carriage return

        char* arg = malloc(bulk_len + 1);
        memcpy(arg, &buffer[pos], bulk_len); // copy the data into the allocated memory
        arg[bulk_len] = '\0'; // null terminate the string

        arguments[(*argument_count)++] = arg; // add the argument to the array
        *length += bulk_len; // increment the length by the length of the argument
        pos += bulk_len; // skip the argument
        pos += 2; // skip the new line and carriage return
    }

    strcpy(command, arguments[0]);
}
