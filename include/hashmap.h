// hashmap.h - simple open addressing hashmap for string keys
#ifndef HASHMAP_H
#define HASHMAP_H

#include <stddef.h>

struct hashmap_entry {
    char *key;
    void *value;
    struct hashmap_entry *next;
};

struct hashmap {
    struct hashmap_entry **buckets;
    size_t num_buckets;
    size_t size;
};

void hashmap_init(struct hashmap *map, size_t num_buckets);
void hashmap_free(struct hashmap *map, void (*free_value)(void *));
void *hashmap_get(struct hashmap *map, const char *key);
int hashmap_put(struct hashmap *map, const char *key, void *value);
int hashmap_remove(struct hashmap *map, const char *key, void (*free_value)(void *));
void hashmap_iter(struct hashmap *map, void (*cb)(const char *, void *, void *), void *user);

#endif // HASHMAP_H
