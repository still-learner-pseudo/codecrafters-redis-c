#ifndef BLOCKING_WAIT_H
#define BLOCKING_WAIT_H

#include "double_linked_list.h"
#include "hashmap.h"

typedef struct waiting_client_blpop {
    int fd;
    char* key;
    long long start_time_ms;
    int timeout_ms;
    struct waiting_client_blpop* next;
    struct waiting_client_blpop* prev;
} waiting_client_blpop;

typedef struct waiting_client_xread {
    int fd;
    int num_keys;
    char** keys;
    char** last_ids;
    long long start_time_ms;
    int timeout_ms;
    struct waiting_client_xread* next;
    struct waiting_client_xread* prev;
} waiting_client_xread;


extern list* blpop_waiting_clients;
extern list* xread_waiting_clients;
extern hashmap map;

long long current_time_ms();
void handle_blpop(char* key, int fd, int timeout_ms);
void notify_blpop_clients(char* key);
void check_blpop_timeouts();
void remove_blpop_client(int fd);

// XREAD
void handle_xread_block(char** keys, char** ids, int num_keys, int fd, int timeout_ms);
void notify_xread_clients(const char* key);
void check_xread_timeouts();
void remove_xread_client(int fd);

#endif
