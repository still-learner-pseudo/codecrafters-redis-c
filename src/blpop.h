#ifndef BLPOP_H
#define BLPOP_H

#include "double_linked_list.h"
#include "hashmap.h"

typedef struct waiting_client {
    int fd;
    char* key;
    long long start_time_ms;
    int timeout_ms;
    struct waiting_client* next;
    struct waiting_client* prev;
} waiting_client;

extern list* blpop_waiting_clients;
extern hashmap map;

long long current_time_ms();
void handle_blpop(char* key, int fd, int timeout_ms);
void notify_blpop_clients(char* key);
void check_blpop_timeouts();
void remove_blpop_client(int fd);

#endif
