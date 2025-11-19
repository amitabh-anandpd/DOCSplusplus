// hashmap.c - simple open addressing hashmap for string keys
#include "../../include/hashmap.h"
#include <stdlib.h>
#include <string.h>

static unsigned long hash_string(const char *str) {
    unsigned long hash = 5381;
    int c;
    while ((c = *str++)) hash = ((hash << 5) + hash) + c;
    return hash;
}

void hashmap_init(struct hashmap *map, size_t num_buckets) {
    map->buckets = calloc(num_buckets, sizeof(struct hashmap_entry *));
    map->num_buckets = num_buckets;
    map->size = 0;
}

void hashmap_free(struct hashmap *map, void (*free_value)(void *)) {
    for (size_t i = 0; i < map->num_buckets; ++i) {
        struct hashmap_entry *entry = map->buckets[i];
        while (entry) {
            struct hashmap_entry *next = entry->next;
            free(entry->key);
            if (free_value) free_value(entry->value);
            free(entry);
            entry = next;
        }
    }
    free(map->buckets);
    map->buckets = NULL;
    map->num_buckets = 0;
    map->size = 0;
}

void *hashmap_get(struct hashmap *map, const char *key) {
    unsigned long h = hash_string(key) % map->num_buckets;
    struct hashmap_entry *entry = map->buckets[h];
    while (entry) {
        if (strcmp(entry->key, key) == 0) return entry->value;
        entry = entry->next;
    }
    return NULL;
}

int hashmap_put(struct hashmap *map, const char *key, void *value) {
    unsigned long h = hash_string(key) % map->num_buckets;
    struct hashmap_entry *entry = map->buckets[h];
    while (entry) {
        if (strcmp(entry->key, key) == 0) {
            entry->value = value;
            return 0;
        }
        entry = entry->next;
    }
    entry = malloc(sizeof(struct hashmap_entry));
    if (!entry) return -1;
    entry->key = strdup(key);
    entry->value = value;
    entry->next = map->buckets[h];
    map->buckets[h] = entry;
    map->size++;
    return 0;
}

int hashmap_remove(struct hashmap *map, const char *key, void (*free_value)(void *)) {
    unsigned long h = hash_string(key) % map->num_buckets;
    struct hashmap_entry **pp = &map->buckets[h];
    while (*pp) {
        struct hashmap_entry *entry = *pp;
        if (strcmp(entry->key, key) == 0) {
            *pp = entry->next;
            free(entry->key);
            if (free_value) free_value(entry->value);
            free(entry);
            map->size--;
            return 0;
        }
        pp = &entry->next;
    }
    return -1;
}

void hashmap_iter(struct hashmap *map, void (*cb)(const char *, void *, void *), void *user) {
    for (size_t i = 0; i < map->num_buckets; ++i) {
        struct hashmap_entry *entry = map->buckets[i];
        while (entry) {
            cb(entry->key, entry->value, user);
            entry = entry->next;
        }
    }
}
