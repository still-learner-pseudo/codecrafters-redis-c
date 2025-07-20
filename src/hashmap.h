#ifndef HASHMAP_H
#define HASHMAP_H

#include <stdlib.h>
#include <string.h>

typedef struct {
    long long timestamp;
    int time_to_live;
} metadata;

typedef struct hashmap_entry {
    char* key;
    char* value;
    metadata* metadata;
    struct hashmap_entry* next;
} hashmap_entry;

typedef struct {
    hashmap_entry* entries;
    size_t capacity;
} hashmap;


void hashmap_create(hashmap* hashmap, size_t capacity);
void hashmap_destroy(hashmap* hashmap);
size_t hash(const char* key);
void hashmap_insert(hashmap* hashmap, const char* key, const char* value, metadata* metadata);
void hashmap_delete_entry(hashmap* hashmap, const char* key);

#endif
