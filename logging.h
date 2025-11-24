#pragma once

#include <stdarg.h>

typedef enum log_category_t {
    LOG_CATEGORY_FILE_PARSING = 0,
    LOG_CATEGORY_MESSAGE_QUEUE,
    LOG_CATEGORY_EMERGENCY_STATUS,
    LOG_CATEGORY_RESCUER_STATUS,
    LOG_CATEGORY_CONFIGURATION,
    LOG_CATEGORY_SYSTEM,
    LOG_CATEGORY_COUNT
} log_category_t;

int log_init(const char* path);
void log_shutdown(void);
void log_event(log_category_t category, const char* id, const char* fmt, ...);
void log_event_v(log_category_t category, const char* id, const char* fmt, va_list args);

const char* log_category_to_string(log_category_t category);

#define LOG_FILE_PARSING(id, fmt, ...) \
    log_event(LOG_CATEGORY_FILE_PARSING, id, fmt, ##__VA_ARGS__)
#define LOG_MESSAGE_QUEUE(id, fmt, ...) \
    log_event(LOG_CATEGORY_MESSAGE_QUEUE, id, fmt, ##__VA_ARGS__)
#define LOG_EMERGENCY_STATUS(id, fmt, ...) \
    log_event(LOG_CATEGORY_EMERGENCY_STATUS, id, fmt, ##__VA_ARGS__)
#define LOG_RESCUER_STATUS(id, fmt, ...) \
    log_event(LOG_CATEGORY_RESCUER_STATUS, id, fmt, ##__VA_ARGS__)
#define LOG_CONFIGURATION(id, fmt, ...) \
    log_event(LOG_CATEGORY_CONFIGURATION, id, fmt, ##__VA_ARGS__)
#define LOG_SYSTEM(id, fmt, ...) \
    log_event(LOG_CATEGORY_SYSTEM, id, fmt, ##__VA_ARGS__)
