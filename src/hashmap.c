#include "hashmap.h"
#include <string.h>

void hashmap_create(hashmap* hashmap, size_t capacity) {
    hashmap->entries = (hashmap_entry*) calloc(capacity, sizeof(hashmap_entry));
    hashmap->capacity = capacity;

    for(size_t i = 0; i < capacity; i++) {
        hashmap->entries[i].key = NULL;
        hashmap->entries[i].value = NULL;
        hashmap->entries[i].metadata = NULL;
        hashmap->entries[i].next = NULL;
    }
}

void hashmap_destroy(hashmap* hashmap) {
    if(hashmap == NULL) {
        return;
    }

    // free all the allocated memory
    for(size_t i = 0; i < hashmap->capacity; i++) {
        hashmap_entry* current = &hashmap->entries[i];

        if(current == NULL)
            continue;

        while(current != NULL) {
            hashmap_entry* next = current->next;
            free(current->key);
            free(current->value);
            free(current->metadata);

            if(current != &hashmap->entries[i])
                free(current);

            current = next;
        }
    }

    // free the hashmap entries that we allocated
    free(hashmap->entries);
}

size_t hash(const char* key) {
    size_t hash = 0;
    for(size_t i = 0; key[i] != '\0'; i++) {
        hash = (hash * 31 + key[i]) % 1000000007;
    }
    return hash;
}

void hashmap_add_entry(hashmap* hashmap, const char* key, const char* value, metadata* metadata) {
    if(hashmap == NULL || key == NULL || value == NULL || metadata == NULL) {
        return;
    }

    // Find the index for the key
    size_t index = hash(key) % hashmap->capacity;
    hashmap_entry* head = &hashmap->entries[index];

    // check if the head is NULL
    if(head->key == NULL) {
        head->key = strdup(key);
        head->value = strdup(value);
        head->metadata = metadata;
        head->next = NULL;
        return;
    }

    hashmap_entry* current = head;
    while(current->next != NULL) {
        if(strcmp(current->key, key) == 0) {
            free(current->value);
            current->value = strdup(value);
            free(current->metadata);
            current->metadata = metadata;

            return;
        }

        if(current->next == NULL)
            break;

        current = current->next;
    }

    hashmap_entry* new_entry = (hashmap_entry*) malloc(sizeof(hashmap_entry));
    new_entry->key = strdup(key);
    new_entry->value = strdup(value);
    new_entry->metadata = metadata;
    new_entry->next = NULL;
    current->next = new_entry;
}

void hashmap_delete_entry(hashmap* hashmap, const char* key) {
    if (hashmap == NULL || key == NULL) return;

    size_t index = hash(key) % hashmap->capacity;
    hashmap_entry* current = &hashmap->entries[index];
    hashmap_entry* prev = NULL;

    while (current != NULL) {
        if (current->key != NULL && strcmp(current->key, key) == 0) {
            // Case 1: deleting the head entry
            if (prev == NULL) {
                if (current->next != NULL) {
                    // Copy next into current
                    hashmap_entry* next = current->next;
                    free(current->key);
                    free(current->value);
                    free(current->metadata);

                    current->key = next->key;
                    current->value = next->value;
                    current->metadata = next->metadata;
                    current->next = next->next;
                    free(next);
                } else {
                    // Free head entry
                    free(current->key);
                    free(current->value);
                    free(current->metadata);
                    current->key = NULL;
                    current->value = NULL;
                    current->metadata = NULL;
                    current->next = NULL;
                }
            }
            // Case 2: deleting non-head node
            else {
                prev->next = current->next;
                free(current->key);
                free(current->value);
                free(current->metadata);
                free(current);
            }
            return;
        }
        prev = current;
        current = current->next;
    }
}
