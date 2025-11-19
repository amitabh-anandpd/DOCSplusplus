#ifndef FILE_INDEX_H
#define FILE_INDEX_H

#include <stddef.h>

#define MAX_SS 32

typedef struct FileMeta {
    char name[256];
    int ss_ids[MAX_SS];
    int ss_count;
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

#endif // FILE_INDEX_H
