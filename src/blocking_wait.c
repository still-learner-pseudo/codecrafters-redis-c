#include "blocking_wait.h"
#include "double_linked_list.h"
#include "hashmap.h"
#include "stream.h"
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/time.h>

list* blpop_waiting_clients = NULL;
list* xread_waiting_clients = NULL;

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
        waiting_client_blpop* wc = malloc(sizeof(waiting_client_blpop));
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
        waiting_client_blpop* wc = (waiting_client_blpop*)node->value;
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
                free(((waiting_client_blpop*)to_remove->value)->key);
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
        waiting_client_blpop* wc = (waiting_client_blpop*)node->value;
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
            free(((waiting_client_blpop*)to_remove->value)->key);
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
        waiting_client_blpop* wc = (waiting_client_blpop*)node->value;
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

// XREAD blocking

void handle_xread_block(char** keys, char** ids, int num_keys, int fd, int timeout_ms) {
    int found = 0; // if there is any stream with new entries
    char buffer[1024 * 8] = {0};
    for (int i = 0; i < num_keys; i++) {
        hashmap_entry* entry = hashmap_find_entry(&map, keys[i]);
        if (entry && entry->value_type == TYPE_STREAM) {
            stream* s = (stream*) entry->value;
            if (xread_stream_entries(s, ids[i], keys[i], buffer)) {
                write(fd, buffer, strlen(buffer));
                found = 1;
                break;
            }
        }
    }
    if (found) return;

    // if there is nothing to XREAD, then block for timeout specified
    waiting_client_xread* wc = malloc(sizeof(waiting_client_xread));
    wc->fd = fd;
    wc->num_keys = num_keys;
    wc->keys = malloc(num_keys * sizeof(char*));
    wc->last_ids = malloc(num_keys * sizeof(char*));
    for (int i = 0; i < num_keys; i++) {
        wc->keys[i] = strdup(keys[i]);
        wc->last_ids[i] = strdup(ids[i]);
    }
    wc->start_time_ms = current_time_ms();
    wc->timeout_ms = timeout_ms;
    wc->next = wc->prev = NULL;
    if (!xread_waiting_clients) xread_waiting_clients = create_list();
    append_to_list(xread_waiting_clients, wc);
}

void notify_xread_clients(const char* key) {
    if (!xread_waiting_clients) return;
    dll* node = xread_waiting_clients->head;
    while (node) {
        waiting_client_xread* wc = (waiting_client_xread*) node->value;
        int found = 0;
        char buffer[1024 * 8] = {0};
        for (int i = 0; i < wc->num_keys; i++) {
            if (strcmp(wc->keys[i], key) == 0) {
                hashmap_entry* entry = hashmap_find_entry(&map, key);
                if (entry && entry->value_type == TYPE_STREAM) {
                    stream* s = (stream*) entry->value;
                    if (xread_stream_entries(s, wc->last_ids[i], wc->keys[i], buffer)) {
                        char top_level[1024 * 8];
                        snprintf(top_level, sizeof(top_level), "*1\r\n%s", buffer); // this is to wrap the result in an array
                        write(wc->fd, top_level, strlen(top_level));
                        found = 1;
                        break;
                    }
                }
            }
        }

        if (found) {
            dll* to_remove = node;
            node = node->next;
            if (to_remove->prev) to_remove->prev->next = to_remove->next;
            if (to_remove->next) to_remove->next->prev = to_remove->prev;
            if (xread_waiting_clients->head == to_remove) xread_waiting_clients->head = to_remove->next;
            if (xread_waiting_clients->tail == to_remove) xread_waiting_clients->tail = to_remove->prev;
            for (int i = 0; i < wc->num_keys; i++) {
                free(wc->keys[i]);
                free(wc->last_ids[i]);
            }
            free(wc->keys);
            free(wc->last_ids);
            free(wc);
            free(to_remove);
            xread_waiting_clients->size--;
        } else {
            node = node->next;
        }
    }
}

void check_xread_timeouts() {
    if (!xread_waiting_clients) return;
    long long now = current_time_ms();
    dll* node = xread_waiting_clients->head;
    while (node) {
        waiting_client_xread* wc = (waiting_client_xread*) node->value;
        if (wc->timeout_ms != -1 && (now - wc->start_time_ms >= wc->timeout_ms)) {
            char buffer[8] = "$-1\r\n";
            write(wc->fd, buffer, strlen(buffer));
            dll* to_remove = node;
            node = node->next;
            if (to_remove->prev) to_remove->prev->next = to_remove->next;
            if (to_remove->next) to_remove->next->prev = to_remove->prev;
            if (xread_waiting_clients->head == to_remove) xread_waiting_clients->head = to_remove->next;
            if (xread_waiting_clients->tail == to_remove) xread_waiting_clients->tail = to_remove->prev;
            for (int i = 0; i < wc->num_keys; i++) {
                free(wc->keys[i]);
                free(wc->last_ids[i]);
            }
            free(wc->keys);
            free(wc->last_ids);
            free(wc);
            free(to_remove);
            xread_waiting_clients->size--;
        } else {
            node = node->next;
        }
    }
}

void remove_xread_client(int fd) {
    if (!xread_waiting_clients) return;
    dll* node = xread_waiting_clients->head;
    while (node) {
        waiting_client_xread* wc = (waiting_client_xread*) node->value;
        if (wc->fd == fd) {
            printf("Removing Xread client with fd %d\n", fd);
            dll* to_remove = node;
            node = node->next;
            if (to_remove->prev) to_remove->prev->next = to_remove->next;
            if (to_remove->next) to_remove->next->prev = to_remove->prev;
            if (xread_waiting_clients->head == to_remove) xread_waiting_clients->head = to_remove->next;
            if (xread_waiting_clients->tail == to_remove) xread_waiting_clients->tail = to_remove->prev;
            for (int i = 0; i < wc->num_keys; i++) {
                free(wc->keys[i]);
                free(wc->last_ids[i]);
            }
            free(wc->keys);
            free(wc->last_ids);
            free(wc);
            free(to_remove);
            xread_waiting_clients->size--;
        } else {
            node = node->next;
        }
    }
}
