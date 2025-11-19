#include "../../include/file_index.h"
#include <stdlib.h>
#include <string.h>

static unsigned long hash_filename(const char *str) {
    unsigned long hash = 5381;
    int c;
    while ((c = *str++)) hash = ((hash << 5) + hash) + c;
    return hash;
}

void file_index_init(FileIndex *index, size_t num_buckets) {
    index->buckets = calloc(num_buckets, sizeof(FileMeta*));
    index->num_buckets = num_buckets;
}

void file_index_free(FileIndex *index) {
    for (size_t i = 0; i < index->num_buckets; ++i) {
        FileMeta *cur = index->buckets[i];
        while (cur) {
            FileMeta *next = cur->next;
            free(cur);
            cur = next;
        }
    }
    free(index->buckets);
}

FileMeta *file_index_get(FileIndex *index, const char *name) {
    unsigned long h = hash_filename(name) % index->num_buckets;
    FileMeta *cur = index->buckets[h];
    while (cur) {
        if (strcmp(cur->name, name) == 0) return cur;
        cur = cur->next;
    }
    return NULL;
}

void file_index_put(FileIndex *index, const char *name, int ss_id) {
    unsigned long h = hash_filename(name) % index->num_buckets;
    FileMeta *cur = index->buckets[h];
    while (cur) {
        if (strcmp(cur->name, name) == 0) {
            // Add ss_id if not present
            int found = 0;
            for (int i = 0; i < cur->ss_count; ++i) if (cur->ss_ids[i] == ss_id) { found = 1; break; }
            if (!found && cur->ss_count < MAX_SS) cur->ss_ids[cur->ss_count++] = ss_id;
            return;
        }
        cur = cur->next;
    }
    // Not found, add new
    FileMeta *meta = calloc(1, sizeof(FileMeta));
    strncpy(meta->name, name, sizeof(meta->name)-1);
    meta->ss_ids[meta->ss_count++] = ss_id;
    meta->next = index->buckets[h];
    index->buckets[h] = meta;
}

void file_index_remove(FileIndex *index, const char *name, int ss_id) {
    unsigned long h = hash_filename(name) % index->num_buckets;
    FileMeta **cur = &index->buckets[h];
    while (*cur) {
        if (strcmp((*cur)->name, name) == 0) {
            // Remove ss_id
            int newc = 0;
            for (int i = 0; i < (*cur)->ss_count; ++i) {
                if ((*cur)->ss_ids[i] != ss_id) (*cur)->ss_ids[newc++] = (*cur)->ss_ids[i];
            }
            (*cur)->ss_count = newc;
            if (newc == 0) {
                // Remove entry
                FileMeta *to_free = *cur;
                *cur = (*cur)->next;
                free(to_free);
            }
            return;
        }
        cur = &(*cur)->next;
    }
}

void file_index_iter(FileIndex *index, void (*cb)(FileMeta *, void *), void *user) {
    for (size_t i = 0; i < index->num_buckets; ++i) {
        FileMeta *cur = index->buckets[i];
        while (cur) {
            cb(cur, user);
            cur = cur->next;
        }
    }
}
