#include "blpop.h"
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/time.h>

list* blpop_waiting_clients = NULL;

long long current_time_ms() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000LL + tv.tv_usec / 1000LL;
}

void handle_blpop(char* key, int fd, int timeout_ms) {
    hashmap_entry* entry = hashmap_find_entry(&map, key);
    list* l = (entry && entry->value_type == TYPE_LIST) ? (list*)entry->value : NULL;

    if (l && l->size > 0) {
        dll* head = l->head;
        char buffer[1024];
        snprintf(buffer, 1024, "*2\r\n$%ld\r\n%s\r\n", strlen((char*) entry->key), (char*) entry->key);
        snprintf(buffer + strlen(buffer), 1024 - strlen(buffer), "$%ld\r\n%s\r\n", strlen((char*) head->value), (char*) head->value);
        write(fd, buffer, strlen(buffer));
        delete_from_head(l);
    } else {
        waiting_client* wc = malloc(sizeof(waiting_client));
        wc->fd = fd;
        wc->key = strdup(key);
        wc->start_time_ms = current_time_ms();
        wc->timeout_ms = timeout_ms;
        wc->next = wc->prev = NULL;
        append_to_list(blpop_waiting_clients, wc);
    }
}

void notify_blpop_clients(char* key) {
    dll* node = blpop_waiting_clients->head;
    while (node) {
        waiting_client* wc = (waiting_client*)node->value;
        if (strcmp(wc->key, key) == 0) {
            hashmap_entry* entry = hashmap_find_entry(&map, key);
            list* l = (entry && entry->value_type == TYPE_LIST) ? (list*)entry->value : NULL;
            if (l && l->size > 0) {
                dll* head = l->head;
                char buffer[1024];
                snprintf(buffer, 1024, "*2\r\n$%ld\r\n%s\r\n", strlen(wc->key), (char*) wc->key);
                snprintf(buffer + strlen(buffer), 1024 - strlen(buffer), "$%ld\r\n%s\r\n", strlen((char*) head->value), (char*) head->value);
                write(wc->fd, buffer, strlen(buffer));
                delete_from_head(l);
                dll* to_remove = node;
                node = node->next;
                if (to_remove->prev) to_remove->prev->next = to_remove->next;
                if (to_remove->next) to_remove->next->prev = to_remove->prev;
                if (blpop_waiting_clients->head == to_remove) blpop_waiting_clients->head = to_remove->next;
                if (blpop_waiting_clients->tail == to_remove) blpop_waiting_clients->tail = to_remove->prev;
                free(((waiting_client*)to_remove->value)->key);
                free(to_remove->value);
                free(to_remove);
                blpop_waiting_clients->size--;
                return;
            }
        }
        node = node->next;
    }
}

void check_blpop_timeouts() {
    long long now = current_time_ms();
    dll* node = blpop_waiting_clients->head;
    while (node) {
        waiting_client* wc = (waiting_client*)node->value;
        if (wc->timeout_ms != -1 && (now - wc->start_time_ms >= wc->timeout_ms)) {
            char buffer[1024];
            strcpy(buffer, "$-1\r\n");
            write(wc->fd, buffer, strlen(buffer));
            dll* to_remove = node;
            node = node->next;
            if (to_remove->prev) to_remove->prev->next = to_remove->next;
            if (to_remove->next) to_remove->next->prev = to_remove->prev;
            if (blpop_waiting_clients->head == to_remove) blpop_waiting_clients->head = to_remove->next;
            if (blpop_waiting_clients->tail == to_remove) blpop_waiting_clients->tail = to_remove->prev;
            free(((waiting_client*)to_remove->value)->key);
            free(to_remove->value);
            free(to_remove);
            blpop_waiting_clients->size--;
        } else {
            node = node->next;
        }
    }
}

void remove_blpop_client(int fd) {
    dll* node = blpop_waiting_clients->head;
    while (node) {
        waiting_client* wc = (waiting_client*)node->value;
        if (wc->fd == fd) {
            dll* to_remove = node;
            node = node->next;
            if (to_remove->prev) to_remove->prev->next = to_remove->next;
            if (to_remove->next) to_remove->next->prev = to_remove->prev;
            if (blpop_waiting_clients->head == to_remove) blpop_waiting_clients->head = to_remove->next;
            if (blpop_waiting_clients->tail == to_remove) blpop_waiting_clients->tail = to_remove->prev;
            free(wc->key);
            free(wc);
            free(to_remove);
            blpop_waiting_clients->size--;
        } else {
            node = node->next;
        }
    }
}
