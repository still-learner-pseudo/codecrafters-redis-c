#ifndef STREAM_H
#define STREAM_H

#include <stdlib.h>
#include <string.h>
#include <limits.h>

typedef struct stream_entry {
    char* id;
    char** fields;
    char** values;
    int num_fields;
    struct stream_entry* next;
} stream_entry;

typedef struct stream {
    stream_entry* head;
    stream_entry* tail;
    int size;
} stream;

stream* create_stream();
void destroy_stream(stream* s);
void add_stream_entry(stream* s, const char* id, char** fields, char** values, int num_fields);
void get_last_id(stream* s, char* id);
void parse_stream_id(char* id, long long* time_ms, int* sequence);
int compare_stream_id(long long time_ms1, int sequence1, long long time_ms2, int sequence2);
void get_stream_entries(stream* s, char* start_id, char* end_id, char* buffer);

#endif
