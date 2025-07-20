#include "double_linked_list.h"

list* create_list() {
    list* new_list = malloc(sizeof(list));
    if(!new_list) {
        return NULL;
    }
    new_list->head = NULL;
    new_list->tail = NULL;
    new_list->size = 0;

    return new_list;
}

void destroy_list(list* list) {
    if(!list) {
        return;
    }

    dll* current = list->head;
    dll* next = NULL;

    while(current) {
        next = current->next;
        free(current);
        current = next;
    }

    free(list);
}

void append_to_list(list* list, void* value) {
    dll* new_node = malloc(sizeof(dll));
    if(!new_node) {
        return;
    }

    new_node->value = value;
    new_node->next = NULL;
    new_node->prev = list->tail;

    if(list->tail) {
        list->tail->next = new_node;
    }

    list->tail = new_node;

    if(!list->head) {
        list->head = new_node;
    }

    list->size++;
}

void prepend_to_list(list *list, void *value) {
    dll* new_node = malloc(sizeof(dll));
    if(!new_node) {
        return;
    }

    new_node->value = value;
    new_node->next = list->head;
    new_node->prev = NULL;

    if(list->head) {
        list->head->prev = new_node;
    }

    list->head = new_node;
    if(!list->tail) {
        list->tail = new_node;
    }

    list->size++;
}

void delete_from_head(list* list) {
    if(!list || !list->head) {
        return;
    }

    dll* old_head = list->head;
    list->head = old_head->next;

    if(list->head) {
        list->head->prev = NULL;
    } else {
        list->tail = NULL;
    }

    free(old_head);
    list->size--;
}

void get_values_array(list* list, int start, int end, char* buffer) {
    if(!list) { // If the list does not exist, an empty array is returned
        strcpy(buffer, "*0\r\n");
        return;
    }

    if (start < 0) start += list->size;
    if (end < 0) end += list->size;

    if (start < 0) start = 0;
    if (end >= list->size) end = list->size - 1;

    if(start >= list->size || start > end) { // If the start index is greater than or equal to the list's length, an empty array is returned.
        strcpy(buffer, "*0\r\n");
        return;
    }

    size_t count = end - start + 1;
    snprintf(buffer, 1024, "*%zu\r\n", count);

    for(size_t i = start; i <= end; i++) {
        dll* current = list->head;
        for(size_t j = 0; j < i; j++) {
            current = current->next;
        }
        snprintf(buffer + strlen(buffer), 1024 - strlen(buffer), "$%zu\r\n%s\r\n", strlen(current->value), (char*) current->value);
        current = current->next;
    }

    return;
}
