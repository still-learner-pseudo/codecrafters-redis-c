#include "client.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

client* create_client(int fd) {
    client* c = (client*)malloc(sizeof(client));
    if (!c) return NULL;
    c->fd = fd;
    c->in_multi = 0;
    c->queued_commands = NULL;
    c->command_count = 0;
    c->role = CLIENT_ROLE_UNKNOWN;
    c->next = NULL;
    return c;
}

void add_client(client** head, client* new_client) {
    new_client->next = *head;
    *head = new_client;
}

void remove_and_destroy_client(int fd, client** head) {
    client* current = *head;
    client* prev = NULL;
    while (current != NULL && current->fd != fd) {
        prev = current;
        current = current->next;
    }
    if (current == NULL) return;
    if (prev == NULL) {
        *head = current->next;
    } else {
        prev->next = current->next;
    }
    destroy_client(current);
}

void destroy_client(client* c) {
    if (!c) return;
    clear_command_queue(c);
    free(c);
}

void destroy_all_clients(client** head) {
    client* current = *head;
    while (current != NULL) {
        client* next = current->next;
        destroy_client(current);
        current = next;
    }
    *head = NULL;
}

client* find_client(int fd, client* head) {
    client* current = head;
    while (current != NULL) {
        if (current->fd == fd) {
            return current;
        }
        current = current->next;
    }
    return NULL;
}

void add_command_to_queue(client* c, const char* command_buffer) {
    queued_buffer* new_command = (queued_buffer*)malloc(sizeof(queued_buffer));
    new_command->buffer = strdup(command_buffer);
    new_command->next = NULL;

    if (c->queued_commands == NULL) {
        c->queued_commands = new_command;
    } else {
        queued_buffer* current = c->queued_commands;
        while (current->next != NULL) {
            current = current->next;
        }
        current->next = new_command;
    }
    c->command_count++;
}

void clear_command_queue(client* c) {
    queued_buffer* current = c->queued_commands;
    while (current != NULL) {
        queued_buffer* next = current->next;
        free(current->buffer);
        free(current);
        current = next;
    }
    c->queued_commands = NULL;
    c->command_count = 0;
}
