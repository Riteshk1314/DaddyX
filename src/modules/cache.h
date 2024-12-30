// src/modules/cache.h
#ifndef CACHE_H
#define CACHE_H

#include <time.h>
#include <stddef.h>

// Cache entry structure
typedef struct {
    char* key;              // Cache key (usually URL)
    char* data;            // Cached response data
    size_t data_size;      // Size of cached data
    time_t timestamp;      // When the entry was cached
    time_t expires;        // When the entry expires
} cache_entry_t;

// Cache configuration structure
typedef struct {
    size_t max_entries;        // Maximum number of entries in cache
    size_t max_memory;         // Maximum memory usage in bytes
    time_t default_ttl;        // Default time-to-live for cache entries
    cache_entry_t* entries;    // Array of cache entries
    size_t current_entries;    // Current number of entries
} cache_config_t;

// Function prototypes
cache_config_t* cache_init(size_t max_entries, size_t max_memory, time_t default_ttl);
void cache_free(cache_config_t* config);
int cache_put(cache_config_t* config, const char* key, const char* data, size_t data_size);
cache_entry_t* cache_get(cache_config_t* config, const char* key);
void cache_remove(cache_config_t* config, const char* key);

#endif // CACHE_H