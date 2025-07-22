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
#include <sys/epoll.h>
#include "hashmap.h"
#include "double_linked_list.h"
#include "blpop.h"
#include "stream.h"

#define MAX_CLIENTS 10
#define CAPACITY 1000

hashmap map;

void handle_input(char* buffer, int fd);
void parse_input(char* buffer, char* command, char** arguments, int* argument_count, int* length);
// these are for key-value operations
void set_key_value(char* key, char* value, char* buffer, char* option, int time_to_live);
void get_key_value(char* key, char* buffer);
void get_key_type(char* key, char* buffer);
void delete_key_value(char* key);
// these are for list operations
void push_to_list(char* command, char* key, char** arguments, int arguments_count, char* buffer);
void range_list(char* key, int start, int end, char* buffer);
void get_list_length(char* key, char* buffer);
void pop_from_list(char* key, int count, char* buffer);
void blpop(char* key, int );

void add_to_stream(char* key, char* id, char* field, char* value, char* buffer);
char* validate_id(char* id, char* top_id, long long* timestamp_ms, int* sequence_number, char* buffer);

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

	int epoll_fd = epoll_create1(0);
	if (epoll_fd == -1) {
		printf("epoll_create1 failed: %s \n", strerror(errno));
		return 1;
	}

	// add the server socket to the epoll-list
	struct epoll_event ev, events[MAX_CLIENTS];
	ev.events = EPOLLIN;
    ev.data.fd = server_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &ev) == -1) {
        printf("epoll_ctl: server_fd failed: %s\n", strerror(errno));
        return 1;
    }

	int num_clients = 0;
	int client_fds[MAX_CLIENTS];

	// initialize hashmap
	hashmap_create(&map, CAPACITY);
	blpop_waiting_clients = create_list();

	while(1) {
	    // wait on the epoll-list
    	int epoll_event_count = epoll_wait(epoll_fd, events, MAX_CLIENTS, 100); // wait for 100ms(added this for blpop)
        if (epoll_event_count == -1) {
            printf("epoll_wait failed: %s \n", strerror(errno));
            break;
        }

        check_blpop_timeouts(); // check and remove expired clients

        for (int i = 0; i < epoll_event_count; i++) {
            int fd = events[i].data.fd;
            if(fd == server_fd) {
                int new_client_fd = accept(server_fd, (struct sockaddr*) &client_addr, &client_addr_len);
                if(new_client_fd > 0) {
                    if(num_clients < MAX_CLIENTS) {
                        printf("Client %d Connected\n", num_clients);
                        client_fds[num_clients++] = new_client_fd;

                        // add the new client also to the epoll-list
                        ev.events = EPOLLIN;
                        ev.data.fd = new_client_fd;
                        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, new_client_fd, &ev) == -1) {
                            printf("epoll_ctl: client_fd failed: %s\n", strerror(errno));
                            close(new_client_fd);
                            continue;
                        }
                    } else {
                        printf("Maximum number of clients reached\n");
                        close(new_client_fd);
                    }
                }
            } else {
                char buffer[1024];
                ssize_t bytes_read = read(fd, buffer, sizeof(buffer));
                if(bytes_read > 0) {
                    buffer[bytes_read] = '\0';
                    printf("Client %d: %s\n", fd, buffer);
                    handle_input(buffer, fd);
                    printf("Server output: %s\n", buffer);
                    if(strlen(buffer) > 0) {
                        write(fd, buffer, strlen(buffer));
                    }
                } else if(bytes_read == 0) {
                    printf("Client %d disconnected\n", i);
                    close(fd);
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);

                    for(int j = 0; j < num_clients; j++) {
                        if(client_fds[j] == fd) {
                            for (int k = j; k < num_clients - 1; k++) {
                                client_fds[k] = client_fds[k + 1];
                            }
                            num_clients--;
                            break;
                        }
                    }
                } else {
                    printf("Error reading from client %d: %s\n", fd, strerror(errno));
                }
            }
        }
	}

	close(server_fd);
	hashmap_destroy(&map);

	return 0;
}

void handle_input(char* buffer, int fd) {
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
    } else if (strcmp(command, "TYPE") == 0) {
        if (arguments_count < 2) { // if there are less than 2 arguments that means only the command TYPE was sent, key is missing
            strcpy(buffer, "-ERR wrong number of arguments for 'type' command\r\n");
        } else {
            get_key_type(arguments[1], buffer);
        }
    } else if ((strcmp(command, "RPUSH") == 0) || (strcmp(command, "LPUSH") == 0)) {
        if (arguments_count < 3) {
            strcpy(buffer, "-ERR wrong number of arguments for 'rpush' command\r\n");
        } else {
            push_to_list(command, arguments[1], arguments, arguments_count, buffer);
            notify_blpop_clients(arguments[1]); // to handle BLPOP command
        }
    } else if (strcmp(command, "LRANGE") == 0) {
        if (arguments_count < 4) {
            strcpy(buffer, "-ERR wrong number of arguments for 'lrange' command\r\n");
        } else {
            range_list(arguments[1], atoi(arguments[2]), atoi(arguments[3]), buffer);
        }
    } else if (strcmp(command, "LLEN") == 0) {
        if (arguments_count < 2) {
            strcpy(buffer, "-ERR wrong number of arguments for 'llen' command\r\n");
        } else {
            get_list_length(arguments[1], buffer);
        }
    } else if (strcmp(command, "LPOP") == 0) {
        if (arguments_count < 2) {
            strcpy(buffer, "-ERR wrong number of arguments for 'lpop' command\r\n");
        } else if (arguments_count == 2) {
            pop_from_list(arguments[1], 1, buffer);
        } else if (arguments_count == 3) {
            pop_from_list(arguments[1], atoi(arguments[2]), buffer);
        }
    } else if (strcmp(command, "BLPOP") == 0) {
        if (arguments_count < 3) {
            strcpy(buffer, "-ERR wrong number of arguments for 'blpop' command\r\n");
        } else if (arguments_count == 3) {
            // handle the multiple client requests by blocking and serving the clients one by one in order
            double timeout = atof(arguments[2]);
            double timeout_sec = atof(arguments[2]);
            int timeout_ms = (timeout_sec == 0.0) ? -1 : (int)(timeout_sec * 1000.0);
            handle_blpop(arguments[1], fd, timeout_ms);
            buffer[0] = '\0';
        }
    } else if (strcmp(command, "XADD") == 0) {
        if (arguments_count < 5) {
            strcpy(buffer, "-ERR wrong number of arguments for 'xadd' command\r\n");
        } else if (arguments_count >= 5 && arguments_count % 2 == 1) {
            add_to_stream(arguments[1], arguments[2], arguments[3], arguments[4], buffer);
        }
    }

    for(int i = 0; i < arguments_count; i++) {
        free(arguments[i]);
    }
}

// this function sets the key value pair in the hashmap, for now ignoring the options value
void set_key_value(char* key, char* value, char* buffer, char* options, int time_to_live) {
    metadata* metadata_value = malloc(sizeof(metadata));
    if (!metadata_value) {
        strcpy(buffer, "-ERR out of memory\r\n");
        return;
    }

    struct timeval tv;
    gettimeofday(&tv, NULL);
    metadata_value->timestamp = tv.tv_sec * 1000LL + tv.tv_usec / 1000LL;
    metadata_value->time_to_live = time_to_live;

    char* value_copy = strdup(value);
    if (!value_copy) {
        strcpy(buffer, "-ERR out of memory\r\n");
        free(metadata_value);
        return;
    }

    hashmap_add_entry(&map, key,(void*) value_copy, metadata_value, TYPE_STRING);
    strcpy(buffer, "+OK\r\n");

    free(metadata_value);
}

void get_key_value(char* key, char* buffer) {
    size_t index = hash(key) % map.capacity;
    hashmap_entry* current = &map.entries[index];
    struct timeval now;

    while (current != NULL && current->key != NULL) {
        if (strcmp(current->key, key) == 0) {
            if (current->metadata && current->metadata->time_to_live > 0) {
                gettimeofday(&now, NULL);
                long long elapsed = now.tv_sec * 1000LL + now.tv_usec / 1000LL - current->metadata->timestamp;
                if (elapsed >= current->metadata->time_to_live) {
                    hashmap_delete_entry(&map, key);
                    strcpy(buffer, "$-1\r\n");
                    return;
                }
            }

            if (current->value_type == TYPE_STRING) {
                snprintf(buffer, 1024, "$%ld\r\n%s\r\n", strlen(current->value),(char*) current->value);
                return;
            } else if (current->value_type == TYPE_LIST) {
                // do nothing for now
                return;
            } else {
                strcpy(buffer, "$-1\r\n");
            }
        }
        current = current->next;
    }

    strcpy(buffer, "$-1\r\n");
}

void get_key_type(char* key, char* buffer) {
    hashmap_entry* entry = hashmap_find_entry(&map, key);
    if (entry == NULL) {
        strcpy(buffer, "+none\r\n");
        return;
    }
    switch (entry->value_type) {
        case TYPE_STRING:
            strcpy(buffer, "+string\r\n");
            break;
        case TYPE_LIST:
            strcpy(buffer, "+list\r\n");
            break;
        case TYPE_STREAM:
            strcpy(buffer, "+stream\r\n");
            break;
        default:
            strcpy(buffer, "+none\r\n");
    }
}

void delete_key_value(char* key) {
    hashmap_delete_entry(&map, key);
}

void push_to_list(char* command, char* key, char** arguments, int arguments_count, char* buffer) {
    hashmap_entry* entry = hashmap_find_entry(&map, key);
    list* push_list;

    if (entry == NULL) {
        push_list = create_list();
        if (!push_list) {
            strcpy(buffer, "-ERR\r\n");
            return;
        }
        metadata* metadata_value = malloc(sizeof(metadata));
        metadata_value->timestamp = 0;
        metadata_value->time_to_live = -1;

        hashmap_add_entry(&map, key, push_list, metadata_value, TYPE_LIST);
    } else {
        if (entry->value_type != TYPE_LIST) {
            strcpy(buffer, "-ERR\r\n");
            return;
        }
        push_list = (list*)entry->value;
    }

    if (strcmp(command, "RPUSH") == 0 ) {
        for(int i = 2; i < arguments_count; i++) { // skip first two arguments i.e command and key
            char* val = strdup(arguments[i]);
            append_to_list(push_list, (void*) val);
        }
    } else if (strcmp(command, "LPUSH") == 0) {
        for(int i = 2; i < arguments_count; i++) { // skip first two arguments i.e command and key
            char* val = strdup(arguments[i]);
            prepend_to_list(push_list, (void*) val);
        }
    }

    snprintf(buffer, 1024, ":%ld\r\n", push_list->size);
}

void range_list(char* key, int start, int end, char* buffer) {
    char temp[1024];
    hashmap_entry* entry = hashmap_find_entry(&map, key);
    list* push_list = NULL;

    if (entry != NULL && entry->value_type == TYPE_LIST) {
        push_list = (list*) entry->value;
    } else {
        strcpy(buffer, "*0\r\n");
        return;
    }

    get_values_array(push_list, start, end, temp);
    strcpy(buffer, temp);
}

void get_list_length(char* key, char* buffer) {
    hashmap_entry* entry = hashmap_find_entry(&map, key);
    if (entry != NULL && entry->value_type == TYPE_LIST) {
        list* push_list = (list*) entry->value;
        snprintf(buffer, 1024, ":%ld\r\n", push_list->size);
    } else {
        strcpy(buffer, ":0\r\n");
    }
}

void pop_from_list(char* key, int count, char* buffer) {
    if (count < 0) {
        strcpy(buffer, "-ERR invalid count\r\n");
    }

    hashmap_entry* entry = hashmap_find_entry(&map, key);
    if (entry != NULL && entry->value_type == TYPE_LIST) {
        list* current_list = entry->value;
        if (count >= current_list->size) {
            count = current_list->size;
        }

        if (count == 0 || !current_list->head) {
            strcpy(buffer, "$-1\r\n");
        } else if (count == 1) {
            dll* head = current_list->head;
            snprintf(buffer, 1024, "$%ld\r\n%s\r\n", strlen(head->value),(char*) head->value);
            delete_from_head(current_list);
        } else {
            get_values_array(current_list, 0, count - 1, buffer); // get all the values from the list

            for(int i = 0; i < count; i++) { // delete the elements from the list
                delete_from_head(current_list);
            }
        }
    } else {
        strcpy(buffer, "$-1\r\n");
    }
}

void add_to_stream(char* key, char* id, char* field, char* value, char* buffer) {
    hashmap_entry* entry = hashmap_find_entry(&map, key);
    stream* s;
    if (!entry) {
        s = create_stream();
        metadata* metadata_value = malloc(sizeof(metadata));
        metadata_value->timestamp = 0;
        metadata_value->time_to_live = -1;
        hashmap_add_entry(&map, key, s, metadata_value, TYPE_STREAM);
    } else {
        if (entry->value_type != TYPE_STREAM) {
            strcpy(buffer, "$-1\r\n");
            return;
        }
        s = (stream*) entry->value;
    }

    char top_id[256] = "";
    get_last_id(s, top_id);

    long long timestamp_ms;
    int sequence_number;
    char* new_id = validate_id(id, top_id, &timestamp_ms, &sequence_number, buffer);

    if (buffer[0] == '-' || !new_id) {
        return;
    }

    char* fields[1] = { field };
    char* values[1] = { value };
    add_stream_entry(s, new_id, fields, values, 1);

    snprintf(buffer, 1024, "$%ld\r\n%s\r\n", strlen(new_id), new_id);
    free(new_id);
}

char* validate_id(char* id, char* top_id, long long* timestamp_ms, int* sequence_number, char* buffer) {
    char* new_id = NULL;

    new_id = malloc(256);
    if (!new_id) {
        strcpy(buffer, "-ERR Out of memory\r\n");
        return NULL;
    }

    if (sscanf(id, "%lld-%d", timestamp_ms, sequence_number) == 2) {
        if (*timestamp_ms == 0 && *sequence_number == 0) {
            strcpy(buffer, "-ERR The ID specified in XADD must be greater than 0-0\r\n");
            free(new_id);
            return NULL;
        }
        if (top_id[0] != '\0') {
            long long top_ts = -1;
            int top_seq = -1;
            sscanf(top_id, "%lld-%d", &top_ts, &top_seq);
            if (*timestamp_ms < top_ts ||
                (*timestamp_ms == top_ts && *sequence_number <= top_seq)) {
                strcpy(buffer, "-ERR The ID specified in XADD is equal or smaller than the target stream top item\r\n");
                free(new_id);
                return NULL;
            }
        }
        sprintf(new_id, "%lld-%d", *timestamp_ms, *sequence_number);
        return new_id;
    }

    if (sscanf(id, "%lld-%d", timestamp_ms, sequence_number) != 2) {
        if (id[0] == '*') {
            struct timeval tv;
            gettimeofday(&tv, NULL);
            *timestamp_ms = tv.tv_sec * 1000 + tv.tv_usec / 1000;
            *sequence_number = -1;
        } else if (id[strlen(id) - 1] == '*') {
            char ts_buf[256];
            strncpy(ts_buf, id, strlen(id) - 1);
            ts_buf[strlen(id) - 1] = '\0';
            *timestamp_ms = atoll(ts_buf);

            long long top_timestamp_ms = -1;
            int top_sequence_number = -1;
            if (top_id[0] != '\0') {
                sscanf(top_id, "%lld-%d", &top_timestamp_ms, &top_sequence_number);
            }

            if (top_id[0] == '\0' || *timestamp_ms != top_timestamp_ms) {
                *sequence_number = (*timestamp_ms == 0) ? 1 : 0;
            } else {
                *sequence_number = top_sequence_number + 1;
            }
        }

    }


    if (*timestamp_ms == 0 && *sequence_number == 0) {
        strcpy(buffer, "-ERR The ID specified in XADD must be greater than 0-0\r\n");
        return new_id;
    }

    if(top_id[0] == '\0') {
        if (*timestamp_ms == 0) {
            *sequence_number = 1;
        } else {
            *sequence_number = 0;
        }
        sprintf(new_id, "%lld-%d", *timestamp_ms, *sequence_number);
        return new_id;
    }

    if(top_id[0] != '\0') {
        long long top_timestamp_ms = -1;
        int top_sequence_number = -1;
        if (sscanf(top_id, "%lld-%d", &top_timestamp_ms, &top_sequence_number) != 2) {
            strcpy(buffer, "-ERR Invalid top ID format\r\n");
            return NULL;
        }
        if (*timestamp_ms < top_timestamp_ms || (*timestamp_ms == top_timestamp_ms && *sequence_number <= top_sequence_number)) {
            strcpy(buffer, "-ERR The ID specified in XADD is equal or smaller than the target stream top item\r\n");
            return NULL;
        }
        sprintf(new_id, "%lld-%d", *timestamp_ms, *sequence_number);
        return new_id;
    }

    return new_id;
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
