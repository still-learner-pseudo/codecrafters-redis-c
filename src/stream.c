#include "stream.h"
#include "double_linked_list.h"

stream* create_stream() {
    stream* s = malloc(sizeof(stream));
    if (!s) {
        return NULL;
    }
    s->head = NULL;
    s->tail = NULL;
    s->size = 0;
    return s;
}

void destroy_stream(stream* s) {
    stream_entry* entry = s->head;
    while(entry) {
        stream_entry* next = entry->next;
        free(entry->id);
        free(entry->fields);
        free(entry->values);
        free(entry);
        entry = next;
    }
    free(s);
}

void add_stream_entry(stream *s, const char *id, char **fields, char **values, int num_fields) {
    stream_entry* entry = malloc(sizeof(stream_entry));
    entry->id = strdup(id);
    entry->fields = malloc(num_fields * sizeof(char*));
    entry->values = malloc(num_fields * sizeof(char*));
    entry->num_fields = num_fields;
    for (int i = 0; i < num_fields; i++) {
        entry->fields[i] = strdup(fields[i]);
        entry->values[i] = strdup(values[i]);
    }
    entry->next = NULL;
    if (s->tail) {
        s->tail->next = entry;
        s->tail = entry;
    } else {
        s->head = entry;
        s->tail = entry;
    }
    s->size++;
}

void get_last_id(stream* s, char* id) {
    if (!s->tail) {
        return;
    }
    strcpy(id, s->tail->id);
}
