#ifndef FILE_INDEX_H
#define FILE_INDEX_H

#include <stddef.h>
#include <time.h>
#define MAX_SS 32

typedef struct FileMeta {
    char name[256];
    int ss_ids[MAX_SS];
    int ss_count;
    char owner[64];
    time_t created_time;
    time_t last_modified;
    time_t last_accessed;
    char read_users[512];   // comma-separated usernames
    char write_users[512];  // comma-separated usernames
    struct FileMeta *next;
} FileMeta;

typedef struct FileIndex {
    FileMeta **buckets;
    size_t num_buckets;
} FileIndex;

void file_index_init(FileIndex *index, size_t num_buckets);
void file_index_free(FileIndex *index);
FileMeta *file_index_get(FileIndex *index, const char *name);
void file_index_put(FileIndex *index, const char *name, int ss_id);
void file_index_remove(FileIndex *index, const char *name, int ss_id);
void file_index_iter(FileIndex *index, void (*cb)(FileMeta *, void *), void *user);
unsigned long hash_filename(const char *str);

#endif // FILE_INDEX_H
