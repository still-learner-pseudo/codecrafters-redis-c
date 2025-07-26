#include "hashmap.h"
#include "double_linked_list.h"
#include "stream.h"
#include "blocking_wait.h"
#include "client.h"
#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <sys/time.h>
#include <strings.h>

#define MAX_CLIENTS 10
#define CAPACITY 1000

// --- Global Variables ---
hashmap map;
client* client_head = NULL;

// --- Function Prototypes ---
void handle_input(char* request, client* client, char* response);
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

// these are for stream operations
void add_to_stream(char* key, char* id, char** field_value_pairs, int num_pairs, char* buffer);
char* validate_id(char* id, char* top_id, long long* timestamp_ms, int* sequence_number, char* buffer);
void range_from_stream(char* key, char* start_id, char* end_id, char* buffer);
void xread_from_multiple_streams(char**arguments, int arguments_count, char* buffer, int fd);

// these are for incr operations
void incr_key(char* key, char* buffer);

// these are for MULTI_EXEC_DISCARD
void handle_exec_command(client* client, char* buffer);
void handle_discard_command(client* client, char* buffer);
void handle_queue_command(client* client, char* request, char* response);

int main() {
	setbuf(stdout, NULL);
	setbuf(stderr, NULL);
	printf("Logs from your program will appear here!\n");

	int server_fd;
	socklen_t client_addr_len;
	struct sockaddr_in client_addr;

	server_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (server_fd == -1) {
		printf("Socket creation failed: %s...\n", strerror(errno));
		return 1;
	}

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

	if (listen(server_fd, 5) != 0) {
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

	struct epoll_event ev, events[MAX_CLIENTS + 1];
	ev.events = EPOLLIN;
    ev.data.fd = server_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &ev) == -1) {
        printf("epoll_ctl: server_fd failed: %s\n", strerror(errno));
        return 1;
    }

	hashmap_create(&map, CAPACITY);
	blpop_waiting_clients = create_list();
	xread_waiting_clients = create_list();

	while(1) {
    	int epoll_event_count = epoll_wait(epoll_fd, events, MAX_CLIENTS + 1, 100);
        if (epoll_event_count == -1) {
            printf("epoll_wait failed: %s \n", strerror(errno));
            break;
        }

        check_blpop_timeouts();
        check_xread_timeouts();

        for (int i = 0; i < epoll_event_count; i++) {
            int fd = events[i].data.fd;
            if(fd == server_fd) {
                int new_client_fd = accept(server_fd, (struct sockaddr*) &client_addr, &client_addr_len);
                if(new_client_fd > 0) {
                    printf("Client %d Connected\n", new_client_fd);
                    client* new_client = create_client(new_client_fd);
                    add_client(&client_head, new_client);

                    ev.events = EPOLLIN;
                    ev.data.fd = new_client_fd;
                    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, new_client_fd, &ev) == -1) {
                        printf("epoll_ctl: client_fd failed: %s\n", strerror(errno));
                        close(new_client_fd);
                    }
                }
            } else {
                char temp_buffer[1024];
                ssize_t bytes_read = read(fd, temp_buffer, sizeof(temp_buffer) - 1);
                if(bytes_read > 0) {
                    temp_buffer[bytes_read] = '\0';

                    client* c = find_client(fd, client_head);
                    if (!c) {
                        printf("Error: Client %d not found\n", fd);
                        continue;
                    }

                    char* response = malloc(1024 * 8);
                    response[0] = '\0';

                    handle_input(temp_buffer, c, response);

                    if(strlen(response) > 0) {
                        printf("Sending response to client %d: %s\n", fd, response);
                        write(fd, response, strlen(response));
                    }
                    free(response);
                } else {
                    printf("Client %d disconnected\n", fd);
                    close(fd);
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
                    remove_blpop_client(fd);
                    remove_xread_client(fd);
                    remove_and_destroy_client(fd, &client_head);
                }
            }
        }
	}

	close(server_fd);
	hashmap_destroy(&map);
    destroy_all_clients(&client_head);
	return 0;
}

void handle_input(char* request, client* client, char* response) {
    char command[100] = {0};
    char* arguments[100];
    int arguments_count = 0;
    int length = 0;
    parse_input(request, command, arguments, &arguments_count, &length);

    if (arguments_count == 0) {
        goto cleanup;
    }

    response[0] = '\0';

    if (client->in_multi) {
        if (strcasecmp(command, "EXEC") == 0) {
            handle_exec_command(client, response);
        } else if (strcasecmp(command, "DISCARD") == 0) {
            handle_discard_command(client, response);
        } else if (strcasecmp(command, "MULTI") == 0) {
            strcpy(response, "-ERR MULTI calls can not be nested\r\n");
        } else {
            handle_queue_command(client, request, response);
        }
        goto cleanup;
    }

    if (strcasecmp(command, "PING") == 0) {
        strcpy(response, "+PONG\r\n");
    } else if (strcasecmp(command, "ECHO") == 0) {
        if (arguments_count < 2) {
            strcpy(response, "-ERR wrong number of arguments for 'echo' command\r\n");
        } else {
            snprintf(response, 1024, "$%ld\r\n%s\r\n", strlen(arguments[1]), arguments[1]);
        }
    } else if (strcasecmp(command, "SET") == 0) {
        if (arguments_count < 3) {
            strcpy(response, "-ERR wrong number of arguments for 'set' command\r\n");
        } else {
            if(arguments_count == 5 && strcasecmp(arguments[3], "px") == 0)
                set_key_value(arguments[1], arguments[2], response, arguments[3], atoi(arguments[4]));
            else
                set_key_value(arguments[1], arguments[2], response, NULL, -1);
        }
    } else if (strcasecmp(command, "GET") == 0) {
        get_key_value(arguments[1], response);
    } else if (strcasecmp(command, "TYPE") == 0) {
        get_key_type(arguments[1], response);
    } else if (strcasecmp(command, "RPUSH") == 0 || strcasecmp(command, "LPUSH") == 0) {
        if(arguments_count < 3) strcpy(response, "-ERR wrong number of arguments\r\n");
        else {
            push_to_list(command, arguments[1], arguments, arguments_count, response);
            notify_blpop_clients(arguments[1]);
        }
    } else if (strcasecmp(command, "LRANGE") == 0) {
        if(arguments_count < 4) strcpy(response, "-ERR wrong number of arguments\r\n");
        else range_list(arguments[1], atoi(arguments[2]), atoi(arguments[3]), response);
    } else if (strcasecmp(command, "LLEN") == 0) {
        if(arguments_count < 2) strcpy(response, "-ERR wrong number of arguments\r\n");
        else get_list_length(arguments[1], response);
    } else if (strcasecmp(command, "LPOP") == 0) {
        if(arguments_count < 2) strcpy(response, "-ERR wrong number of arguments\r\n");
        else pop_from_list(arguments[1], (arguments_count > 2) ? atoi(arguments[2]) : 1, response);
    } else if (strcasecmp(command, "BLPOP") == 0) {
        if(arguments_count < 3) strcpy(response, "-ERR wrong number of arguments\r\n");
        else {
            double timeout_sec = atof(arguments[arguments_count - 1]);
            int timeout_ms = (int)(timeout_sec * 1000.0);
            if (timeout_sec == 0) timeout_ms = -1;
            handle_blpop(arguments[1], client->fd, timeout_ms);
            response[0] = '\0';
        }
    } else if (strcasecmp(command, "XADD") == 0) {
            // The number of arguments must be odd and at least 5 (XADD key id field value)
            if (arguments_count < 5 || arguments_count % 2 != 1) {
                 strcpy(response, "-ERR wrong number of arguments for 'xadd' command\r\n");
            } else {
                // Pass the key, id, and then a pointer to the start of the field/value pairs
                // and the number of pairs.
                int num_pairs = (arguments_count - 3) / 2;
                add_to_stream(arguments[1], arguments[2], &arguments[3], num_pairs, response);
                notify_xread_clients(arguments[1]); // Notify blocked clients
            }
    } else if (strcasecmp(command, "XRANGE") == 0) {
        if (arguments_count < 4) strcpy(response, "-ERR wrong number of arguments\r\n");
        else range_from_stream(arguments[1], arguments[2], arguments[3], response);
    } else if (strcasecmp(command, "XREAD") == 0) {
        if (arguments_count < 4) strcpy(response, "-ERR wrong number of arguments\r\n");
        else xread_from_multiple_streams(arguments, arguments_count, response, client->fd);
    } else if (strcasecmp(command, "INCR") == 0) {
        if (arguments_count < 2) strcpy(response, "-ERR wrong number of arguments\r\n");
        else incr_key(arguments[1], response);
    } else if (strcasecmp(command, "MULTI") == 0) {
        client->in_multi = 1;
        strcpy(response, "+OK\r\n");
    } else if (strcasecmp(command, "EXEC") == 0) {
        strcpy(response, "-ERR EXEC without MULTI\r\n");
    } else if (strcasecmp(command, "DISCARD") == 0) {
        strcpy(response, "-ERR DISCARD without MULTI\r\n");
    } else {
        strcpy(response, "-ERR unknown command\r\n");
    }

cleanup:
    for(int i = 0; i < arguments_count; i++) {
        free(arguments[i]);
    }
}

void handle_queue_command(client* client, char* request, char* response) {
    if (!client) return;
    add_command_to_queue(client, request);
    strcpy(response, "+QUEUED\r\n");
}

void handle_discard_command(client* client, char* response) {
    if (!client) return;
    clear_command_queue(client);
    client->in_multi = 0;
    strcpy(response, "+OK\r\n");
}

void handle_exec_command(client* client, char* response) {
    if (!client) {
        strcpy(response, "-ERR client error\r\n");
        return;
    }
    int num_commands = client->command_count;
    int offset = sprintf(response, "*%d\r\n", num_commands);
    client->in_multi = 0;
    queued_buffer* current = client->queued_commands;
    while(current != NULL) {
        char temp_response[1024 * 2] = {0};
        handle_input(current->buffer, client, temp_response);
        offset += sprintf(response + offset, "%s", temp_response);
        current = current->next;
    }
    clear_command_queue(client);
}

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
    hashmap_add_entry(&map, key, (void*) value_copy, metadata_value, TYPE_STRING);
    strcpy(buffer, "+OK\r\n");
    free(metadata_value);
}

void get_key_value(char* key, char* buffer) {
    hashmap_entry* entry = hashmap_find_entry(&map, key);
    if (!entry) {
        strcpy(buffer, "$-1\r\n");
        return;
    }
    if (entry->metadata && entry->metadata->time_to_live > 0) {
        struct timeval now;
        gettimeofday(&now, NULL);
        long long elapsed = now.tv_sec * 1000LL + now.tv_usec / 1000LL - entry->metadata->timestamp;
        if (elapsed >= entry->metadata->time_to_live) {
            hashmap_delete_entry(&map, key);
            strcpy(buffer, "$-1\r\n");
            return;
        }
    }
    if (entry->value_type == TYPE_STRING) {
        snprintf(buffer, 1024, "$%ld\r\n%s\r\n", strlen(entry->value), (char*)entry->value);
    } else {
        strcpy(buffer, "-WRONGTYPE Operation against a key holding the wrong kind of value\r\n");
    }
}

void get_key_type(char* key, char* buffer) {
    hashmap_entry* entry = hashmap_find_entry(&map, key);
    if (entry == NULL) {
        strcpy(buffer, "+none\r\n");
        return;
    }
    switch (entry->value_type) {
        case TYPE_STRING: strcpy(buffer, "+string\r\n"); break;
        case TYPE_LIST: strcpy(buffer, "+list\r\n"); break;
        case TYPE_STREAM: strcpy(buffer, "+stream\r\n"); break;
        default: strcpy(buffer, "+none\r\n");
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
        metadata* md = malloc(sizeof(metadata));
        md->timestamp = 0; md->time_to_live = -1;
        hashmap_add_entry(&map, key, push_list, md, TYPE_LIST);
    } else {
        if (entry->value_type != TYPE_LIST) {
            strcpy(buffer, "-WRONGTYPE Operation against a key holding the wrong kind of value\r\n");
            return;
        }
        push_list = (list*)entry->value;
    }
    for(int i = 2; i < arguments_count; i++) {
        char* val = strdup(arguments[i]);
        if (strcasecmp(command, "RPUSH") == 0) append_to_list(push_list, (void*) val);
        else prepend_to_list(push_list, (void*) val);
    }
    snprintf(buffer, 1024, ":%ld\r\n", push_list->size);
}

void range_list(char* key, int start, int end, char* buffer) {
    hashmap_entry* entry = hashmap_find_entry(&map, key);
    if (entry != NULL && entry->value_type == TYPE_LIST) {
        get_values_array((list*)entry->value, start, end, buffer);
    } else {
        strcpy(buffer, "*0\r\n");
    }
}

void get_list_length(char* key, char* buffer) {
    hashmap_entry* entry = hashmap_find_entry(&map, key);
    if (entry != NULL && entry->value_type == TYPE_LIST) {
        snprintf(buffer, 1024, ":%ld\r\n", ((list*)entry->value)->size);
    } else {
        strcpy(buffer, ":0\r\n");
    }
}

void pop_from_list(char* key, int count, char* buffer) {
    hashmap_entry* entry = hashmap_find_entry(&map, key);
    if (entry != NULL && entry->value_type == TYPE_LIST) {
        list* current_list = (list*)entry->value;
        if (count > current_list->size) count = current_list->size;

        if (count == 0 || !current_list->head) {
            strcpy(buffer, "$-1\r\n");
        } else if (count == 1) {
            dll* head = current_list->head;
            snprintf(buffer, 1024, "$%ld\r\n%s\r\n", strlen(head->value),(char*) head->value);
            delete_from_head(current_list);
        } else {
            get_values_array(current_list, 0, count - 1, buffer);
            for(int i = 0; i < count; i++) {
                delete_from_head(current_list);
            }
        }
    } else {
        strcpy(buffer, "$-1\r\n");
    }
}

void add_to_stream(char* key, char* id, char** field_value_pairs, int num_pairs, char* buffer) {
    hashmap_entry* entry = hashmap_find_entry(&map, key);
    stream* s;

    if (!entry) {
        s = create_stream();
        metadata* metadata_value = malloc(sizeof(metadata));
        metadata_value->timestamp = 0;
        metadata_value->time_to_live = -1;
        hashmap_add_entry(&map, key, s, metadata_value, TYPE_STREAM);
        free(metadata_value); // The entry in the hashmap now owns its own copy
    } else {
        if (entry->value_type != TYPE_STREAM) {
            strcpy(buffer, "-WRONGTYPE Operation against a key holding the wrong kind of value\r\n");
            return;
        }
        s = (stream*) entry->value;
    }

    char top_id[256] = "";
    get_last_id(s, top_id);

    long long timestamp_ms;
    int sequence_number;
    char* new_id = validate_id(id, top_id, &timestamp_ms, &sequence_number, buffer);

    // If validate_id returned an error message in the buffer or failed, exit.
    if (!new_id) {
        return;
    }

    // Prepare separate arrays for fields and values for add_stream_entry
    char* fields[num_pairs];
    char* values[num_pairs];
    for (int i = 0; i < num_pairs; i++) {
        fields[i] = field_value_pairs[i * 2];
        values[i] = field_value_pairs[i * 2 + 1];
    }

    add_stream_entry(s, new_id, fields, values, num_pairs);

    snprintf(buffer, 1024, "$%ld\r\n%s\r\n", strlen(new_id), new_id);
    free(new_id); // Clean up the generated ID
}

char* validate_id(char* id, char* top_id, long long* timestamp_ms, int* sequence_number, char* buffer) {
    char* new_id = malloc(256);
    if (!new_id) {
        strcpy(buffer, "-ERR Out of memory\r\n");
        return NULL;
    }

    long long top_ts = -1;
    int top_seq = -1;
    if (top_id[0] != '\0') {
        sscanf(top_id, "%lld-%d", &top_ts, &top_seq);
    }

    long long req_ts = -1;
    int req_seq = -1;

    if (strcmp(id, "*") == 0) {
        struct timeval tv;
        gettimeofday(&tv, NULL);
        req_ts = tv.tv_sec * 1000LL + tv.tv_usec / 1000;
        req_seq = -1;
    } else if (sscanf(id, "%lld-%d", &req_ts, &req_seq) == 2) {
        // do nothing, handling at the end
    } else if (sscanf(id, "%lld-*", &req_ts) == 1 && id[strlen(id)-1] == '*') {
        // do nothing, handling at the end
    } else {
        strcpy(buffer, "-ERR Invalid stream ID specified for XADD\r\n");
        free(new_id);
        return NULL;
    }

    if (req_seq != -1) { // A specific sequence was provided by the user
        *timestamp_ms = req_ts;
        *sequence_number = req_seq;
    } else { // Sequence needs to be auto-generated
        *timestamp_ms = req_ts;
        if (*timestamp_ms > top_ts) {
            *sequence_number = 0;
        } else { // *timestamp_ms <= top_ts
            *timestamp_ms = top_ts; // Ensure timestamp doesn't go backwards
            *sequence_number = top_seq + 1;
        }
    }

    if (*timestamp_ms == 0 && *sequence_number == 0) {
        if (req_seq != -1) {
            strcpy(buffer, "-ERR The ID specified in XADD must be greater than 0-0\r\n");
            free(new_id);
            return NULL;
        }
        *sequence_number = 1;
    }

    if (top_id[0] != '\0' && (*timestamp_ms < top_ts || (*timestamp_ms == top_ts && *sequence_number <= top_seq))) {
        strcpy(buffer, "-ERR The ID specified in XADD is equal or smaller than the target stream top item\r\n");
        free(new_id);
        return NULL;
    }

    sprintf(new_id, "%lld-%d", *timestamp_ms, *sequence_number);
    return new_id;
}

void range_from_stream(char* key, char* start_id, char* end_id, char* buffer) {
    hashmap_entry* entry = hashmap_find_entry(&map, key);
    if (entry && entry->value_type == TYPE_STREAM) {
        get_stream_entries(entry->value, start_id, end_id, buffer);
    } else {
        strcpy(buffer, "*0\r\n");
    }
}

void xread_from_multiple_streams(char** arguments, int arguments_count, char* buffer, int fd) {
    int block_idx = -1, streams_idx = -1;
    for (int i = 1; i < arguments_count; i++) {
        if (strcasecmp(arguments[i], "block") == 0) block_idx = i;
        if (strcasecmp(arguments[i], "streams") == 0) streams_idx = i;
    }

    if (streams_idx == -1) {
        strcpy(buffer, "-ERR syntax error: streams keyword not found\r\n");
        return;
    }

    int num_stream_args = arguments_count - (streams_idx + 1);
    if (num_stream_args <= 0 || num_stream_args % 2 != 0) {
        strcpy(buffer, "-ERR syntax error: incorrect number of stream keys/IDs\r\n");
        return;
    }
    int num_streams = num_stream_args / 2;

    char* keys[num_streams];
    char* ids[num_streams];
    int free_ids_flags[num_streams];
    memset(free_ids_flags, 0, sizeof(free_ids_flags));

    for (int i = 0; i < num_streams; i++) {
        keys[i] = arguments[streams_idx + 1 + i];
        char* id_arg = arguments[streams_idx + 1 + num_streams + i];
        if (strcmp(id_arg, "$") == 0) {
            hashmap_entry* entry = hashmap_find_entry(&map, keys[i]);
            if (entry && entry->value_type == TYPE_STREAM) {
                stream* s = (stream*)entry->value;
                if (s->tail) {
                    ids[i] = strdup(s->tail->id);
                    free_ids_flags[i] = 1;
                } else {
                    ids[i] = "0-0";
                }
            } else {
                ids[i] = "0-0";
            }
        } else {
            ids[i] = id_arg;
        }
    }

    if (block_idx != -1) {
        int timeout_ms = atoi(arguments[block_idx + 1]);
        if (timeout_ms == 0) timeout_ms = -1; // Block indefinitely
        handle_xread_block(keys, ids, num_streams, fd, timeout_ms);
        buffer[0] = '\0'; // Response is handled asynchronously by blocking logic
    } else {
        // Non-blocking XREAD
        char temp[1024 * 16] = {0};
        int stream_count = 0;
        for (int i = 0; i < num_streams; i++) {
            hashmap_entry* entry = hashmap_find_entry(&map, keys[i]);
            if (!entry || entry->value_type != TYPE_STREAM) continue;
            char stream_buffer[1024 * 8] = {0};
            if (xread_stream_entries(entry->value, ids[i], keys[i], stream_buffer)) {
                strcat(temp, stream_buffer);
                stream_count++;
            }
        }
        if (stream_count == 0) {
            strcpy(buffer, "$-1\r\n");
        } else {
            snprintf(buffer, 1024 * 16, "*%d\r\n%s", stream_count, temp);
        }
    }

    // Free any strdup'd IDs for the '$' case
    for(int i = 0; i < num_streams; i++) {
        if(free_ids_flags[i]) free(ids[i]);
    }
}

void incr_key(char* key, char* buffer) {
    hashmap_entry* entry = hashmap_find_entry(&map, key);
    if (!entry) {
        set_key_value(key, "1", buffer, NULL, -1);
        strcpy(buffer, ":1\r\n");
        return;
    }
    if (entry->value_type != TYPE_STRING) {
        strcpy(buffer, "-WRONGTYPE Operation against a key holding the wrong kind of value\r\n");
        return;
    }
    char* value_str = (char*) entry->value;
    char* endptr;
    long value = strtol(value_str, &endptr, 10);
    if (*endptr != '\0') {
        strcpy(buffer, "-ERR value is not an integer or out of range\r\n");
        return;
    }
    value++;
    char new_value[32];
    snprintf(new_value, sizeof(new_value), "%ld", value);
    free(entry->value);
    entry->value = strdup(new_value);
    sprintf(buffer, ":%ld\r\n", value);
}

void parse_input(char* buffer, char* command, char** arguments, int* argument_count, int* length) {
    int pos = 0;
    *argument_count = 0;
    *length = 0;
    if(buffer[0] != '*') return;
    pos++;
    int num_elements = atoi(&buffer[pos]);
    while(buffer[pos] != '\r' && buffer[pos] != '\n') pos++;
    pos += 2;
    for(int i = 0; i < num_elements; i++) {
        if(buffer[pos] != '$') return;
        pos++;
        int bulk_len = atoi(&buffer[pos]);
        while(buffer[pos] != '\r' && buffer[pos] != '\n') pos++;
        pos += 2;
        char* arg = malloc(bulk_len + 1);
        memcpy(arg, &buffer[pos], bulk_len);
        arg[bulk_len] = '\0';
        arguments[(*argument_count)++] = arg;
        *length += bulk_len;
        pos += bulk_len;
        pos += 2;
    }
    if(*argument_count > 0)
        strcpy(command, arguments[0]);
}
