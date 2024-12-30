#include "cache.h"
#include "../utils/logging.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

// Initialize the cache
cache_config_t* cache_init(size_t max_entries, size_t max_memory, time_t default_ttl) {
    cache_config_t* config = (cache_config_t*)malloc(sizeof(cache_config_t));
    if (!config) {
        log_error("Failed to allocate cache config");
        return NULL;
    }

    config->entries = (cache_entry_t*)calloc(max_entries, sizeof(cache_entry_t));
    if (!config->entries) {
        log_error("Failed to allocate cache entries");
        free(config);
        return NULL;
    }

    config->max_entries = max_entries;
    config->max_memory = max_memory;
    config->default_ttl = default_ttl;
    config->current_entries = 0;

    log_info("Cache initialized with max %zu entries, %zu bytes, %ld TTL", 
             max_entries, max_memory, default_ttl);
    return config;
}

// Free cache resources
void cache_free(cache_config_t* config) {
    if (!config) return;

    // Free all entries
    for (size_t i = 0; i < config->current_entries; i++) {
        free(config->entries[i].key);
        free(config->entries[i].data);
    }

    free(config->entries);
    free(config);
}

// Find cache entry by key
static cache_entry_t* find_entry(cache_config_t* config, const char* key) {
    for (size_t i = 0; i < config->current_entries; i++) {
        if (strcmp(config->entries[i].key, key) == 0) {
            return &config->entries[i];
        }
    }
    return NULL;
}

// Remove expired entries
static void cleanup_expired(cache_config_t* config) {
    time_t now = time(NULL);
    size_t i = 0;

    while (i < config->current_entries) {
        if (config->entries[i].expires <= now) {
            // Free entry resources
            free(config->entries[i].key);
            free(config->entries[i].data);

            // Move last entry to this position (if not already last)
            if (i < config->current_entries - 1) {
                config->entries[i] = config->entries[config->current_entries - 1];
            }

            config->current_entries--;
            log_debug("Removed expired cache entry");
        } else {
            i++;
        }
    }
}

// Add or update cache entry
int cache_put(cache_config_t* config, const char* key, const char* data, size_t data_size) {
    if (!config || !key || !data) return -1;

    // Check if entry already exists
    cache_entry_t* existing = find_entry(config, key);
    if (existing) {
        // Update existing entry
        char* new_data = realloc(existing->data, data_size);
        if (!new_data) {
            log_error("Failed to reallocate cache data");
            return -1;
        }

        memcpy(new_data, data, data_size);
        existing->data = new_data;
        existing->data_size = data_size;
        existing->timestamp = time(NULL);
        existing->expires = existing->timestamp + config->default_ttl;
        
        log_debug("Updated cache entry for key: %s", key);
        return 0;
    }

    // Clean up expired entries before adding new one
    cleanup_expired(config);

    // Check if we have room
    if (config->current_entries >= config->max_entries) {
        log_error("Cache is full");
        return -1;
    }

    // Create new entry
    cache_entry_t* entry = &config->entries[config->current_entries];
    
    entry->key = strdup(key);
    if (!entry->key) {
        log_error("Failed to allocate cache key");
        return -1;
    }

    entry->data = malloc(data_size);
    if (!entry->data) {
        log_error("Failed to allocate cache data");
        free(entry->key);
        return -1;
    }

    memcpy(entry->data, data, data_size);
    entry->data_size = data_size;
    entry->timestamp = time(NULL);
    entry->expires = entry->timestamp + config->default_ttl;

    config->current_entries++;
    log_debug("Added new cache entry for key: %s", key);
    return 0;
}

// Retrieve cache entry
cache_entry_t* cache_get(cache_config_t* config, const char* key) {
    if (!config || !key) return NULL;

    cache_entry_t* entry = find_entry(config, key);
    if (!entry) {
        log_debug("Cache miss for key: %s", key);
        return NULL;
    }

    // Check if expired
    if (entry->expires <= time(NULL)) {
        log_debug("Cache entry expired for key: %s", key);
        cache_remove(config, key);
        return NULL;
    }

    log_debug("Cache hit for key: %s", key);
    return entry;
}

// Remove cache entry
void cache_remove(cache_config_t* config, const char* key) {
    if (!config || !key) return;

    for (size_t i = 0; i < config->current_entries; i++) {
        if (strcmp(config->entries[i].key, key) == 0) {
            // Free entry resources
            free(config->entries[i].key);
            free(config->entries[i].data);

            // Move last entry to this position (if not already last)
            if (i < config->current_entries - 1) {
                config->entries[i] = config->entries[config->current_entries - 1];
            }

            config->current_entries--;
            log_debug("Removed cache entry for key: %s", key);
            return;
        }
    }
}