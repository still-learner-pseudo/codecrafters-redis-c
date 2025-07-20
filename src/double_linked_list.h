#ifndef DOUBLE_LINKED_LIST_H
#define DOUBLE_LINKED_LIST_H

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef struct dll {
    struct dll *next;
    struct dll *prev;
    void *value;
} dll;

typedef struct list {
    dll* head;
    dll* tail;
    size_t size;
} list;

list* create_list();
void destroy_list(list* list);
void append_to_list(list* list, void* value);
void prepend_to_list(list* list, void* value);
void delete_from_head(list* list);
void get_values_array(list* list, int start, int end, char* buffer);

#endif
