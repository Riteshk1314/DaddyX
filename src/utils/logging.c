
// src/utils/logging.c
#include "logging.h"
#include <unistd.h>  // getpid()
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

static log_level_t current_log_level = LOG_INFO;
static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

// Initialize logging
void log_init(log_level_t level) {
    current_log_level = level;
}

// Internal logging function
static void log_message(log_level_t level, const char* level_str, const char* format, va_list args) {
    // Skip if message level is below current log level
    if (level < current_log_level) {
        return;
    }

    pthread_mutex_lock(&log_mutex);

    // Get current time
    time_t now;
    time(&now);
    char time_buf[26];
    struct tm* tm_info = localtime(&now);
    strftime(time_buf, 26, "%Y-%m-%d %H:%M:%S", tm_info);

    // Get process ID
    pid_t pid = getpid();

    // Print log header
    fprintf(stderr, "[%s] [%s] [PID:%d] ", time_buf, level_str, pid);

    // Print actual message
    vfprintf(stderr, format, args);
    fprintf(stderr, "\n");

    // Flush immediately for debugging purposes
    fflush(stderr);

    pthread_mutex_unlock(&log_mutex);
}

void log_debug(const char* format, ...) {
    va_list args;
    va_start(args, format);
    log_message(LOG_DEBUG, "DEBUG", format, args);
    va_end(args);
}

void log_info(const char* format, ...) {
    va_list args;
    va_start(args, format);
    log_message(LOG_INFO, "INFO", format, args);
    va_end(args);
}

void log_warn(const char* format, ...) {
    va_list args;
    va_start(args, format);
    log_message(LOG_WARN, "WARN", format, args);
    va_end(args);
}

void log_error(const char* format, ...) {
    va_list args;
    va_start(args, format);
    log_message(LOG_ERROR, "ERROR", format, args);
    va_end(args);
}