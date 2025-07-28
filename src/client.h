#ifndef CLIENT_H
#define CLIENT_H

#include <unistd.h>

typedef struct queued_buffer {
    char* buffer;
    struct queued_buffer* next;
} queued_buffer;

typedef enum {
    CLIENT_ROLE_MASTER,
    CLIENT_ROLE_SLAVE,
    CLIENT_ROLE_UNKNOWN
} client_role;

typedef struct client {
    int fd;
    int in_multi;
    queued_buffer* queued_commands;
    int command_count;
    client_role role;
    struct client* next;
} client;


client* create_client(int fd);
void add_client(client** head, client* new_client);
void remove_and_destroy_client(int fd, client** head);
void destroy_all_clients(client** head);
client* find_client(int fd, client* head);
void add_command_to_queue(client* c, const char* command_buffer);
void clear_command_queue(client* c);
void destroy_client(client* c);

#endif
