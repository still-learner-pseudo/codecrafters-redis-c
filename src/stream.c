#include "stream.h"
#include "double_linked_list.h"
#include <iso646.h>
#include <limits.h>

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

void parse_stream_id(char *id, long long *time_ms, int *sequence) {
    if (strcmp(id, "-") == 0) {
        *time_ms = LLONG_MIN;
        *sequence = INT_MIN;
        return;
    }

    if (strcmp(id, "+") == 0) {
        *time_ms = LLONG_MAX;
        *sequence = INT_MAX;
        return;
    }

    if (sscanf(id, "%lld", time_ms) == 1 && strchr(id, '-') == NULL) {
        *sequence = 0;
        return;
    }

    if (sscanf(id, "%lld-%d", time_ms, sequence) == 2) {
        return;
    }

    *time_ms = -1;
    *sequence = -1;
}

int compare_stream_id(long long time_ms1, int sequence1, long long time_ms2, int sequence2) {
    if (time_ms1 < time_ms2) return -1;
    if (time_ms1 > time_ms2) return 1;
    if (sequence1 < sequence2) return -1;
    if (sequence1 > sequence2) return 1;
    return 0;
}

void get_stream_entries(stream* s, char* start_id, char* end_id, char* buffer) {
    long long start_time_ms, end_time_ms;
    int start_sequence, end_sequence;
    parse_stream_id(start_id, &start_time_ms, &start_sequence);
    parse_stream_id(end_id, &end_time_ms, &end_sequence);

    stream_entry* entry = s->head;
    int count = 0;
    char temp[1024 * 8] = {0};

    while (entry) {
        long long entry_time;
        int entry_sequence;
        parse_stream_id(entry->id, &entry_time, &entry_sequence);

        if (compare_stream_id(entry_time, entry_sequence, start_time_ms, start_sequence) >= 0 &&
            compare_stream_id(entry_time, entry_sequence, end_time_ms, end_sequence) <= 0) {
            char entry_buf[1024] = {0};
            int n = snprintf(entry_buf, sizeof(entry_buf), "*2\r\n$%ld\r\n%s\r\n*%d\r\n", strlen(entry->id), entry->id, 2 * entry->num_fields);
            for (int i = 0; i < entry->num_fields; i++) {
                n += snprintf(entry_buf + n, sizeof(entry_buf) - n, "$%ld\r\n%s\r\n$%ld\r\n%s\r\n",
                    strlen(entry->fields[i]), entry->fields[i], strlen(entry->values[i]), entry->values[i]);
            }
            strcat(temp, entry_buf);
            count++;
        }
        entry = entry->next;
    }

    if (count == 0) {
        strcpy(buffer, "*0\r\n");
    } else {
        snprintf(buffer, 1024 * 8, "*%d\r\n%s", count, temp);
    }
}
