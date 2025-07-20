#ifndef HASHMAP_H
#define HASHMAP_H

#include <stdlib.h>
#include <string.h>
#include "double_linked_list.h"

typedef struct {
    long long timestamp;
    int time_to_live;
} metadata;

typedef enum  {
    TYPE_STRING,
    TYPE_LIST
} value_type;

typedef struct hashmap_entry {
    char* key;
    void* value;
    metadata* metadata;
    value_type value_type;
    struct hashmap_entry* next;
} hashmap_entry;

typedef struct {
    hashmap_entry* entries;
    size_t capacity;
} hashmap;


void hashmap_create(hashmap* hashmap, size_t capacity);
void hashmap_destroy(hashmap* hashmap);
size_t hash(const char* key);
void hashmap_add_entry(hashmap* hashmap, const char* key, void* value, metadata* metadata, value_type value_type);
void hashmap_delete_entry(hashmap* hashmap, const char* key);
hashmap_entry* hashmap_find_entry(hashmap* map, const char* key);

#endif
