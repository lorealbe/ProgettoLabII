#include "logging.h"

#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef LOG_DEFAULT_PATH
#define LOG_DEFAULT_PATH "application.log"
#endif

static FILE* g_log_file = NULL;
static pthread_mutex_t g_log_mutex = PTHREAD_MUTEX_INITIALIZER;
static char g_log_path[FILENAME_MAX] = LOG_DEFAULT_PATH;

static void log_get_timestamp(char* buffer, size_t size) {
    time_t now = time(NULL);
    struct tm tm_info;
#if defined(_POSIX_THREAD_SAFE_FUNCTIONS) && !defined(_WIN32)
    localtime_r(&now, &tm_info);
#else
    struct tm* tmp = localtime(&now);
    if (tmp) {
        tm_info = *tmp;
    } else {
        memset(&tm_info, 0, sizeof(tm_info));
    }
#endif
    strftime(buffer, size, "%Y-%m-%d %H:%M:%S", &tm_info);
}

const char* log_category_to_string(log_category_t category) {
    switch (category) {
        case LOG_CATEGORY_FILE_PARSING:
            return "FILE_PARSING";
        case LOG_CATEGORY_MESSAGE_QUEUE:
            return "MESSAGE_QUEUE";
        case LOG_CATEGORY_EMERGENCY_STATUS:
            return "EMERGENCY_STATUS";
        case LOG_CATEGORY_RESCUER_STATUS:
            return "RESCUER_STATUS";
        case LOG_CATEGORY_CONFIGURATION:
            return "CONFIGURATION";
        case LOG_CATEGORY_SYSTEM:
            return "SYSTEM";
        case LOG_CATEGORY_COUNT:
        default:
            return "UNKNOWN";
    }
}

static int log_open_locked(const char* path) {
    if (g_log_file) {
        if (!path || strcmp(path, g_log_path) == 0) {
            return 0;
        }
        fclose(g_log_file);
        g_log_file = NULL;
    }

    const char* effective_path = path ? path : g_log_path;
    if (path && path[0] != '\0') {
        strncpy(g_log_path, path, sizeof(g_log_path) - 1);
        g_log_path[sizeof(g_log_path) - 1] = '\0';
        effective_path = g_log_path;
    }

    g_log_file = fopen(effective_path, "a");
    if (!g_log_file) {
        return -1;
    }

    setvbuf(g_log_file, NULL, _IOLBF, 0);
    return 0;
}

int log_init(const char* path) {
    int result;
    pthread_mutex_lock(&g_log_mutex);
    result = log_open_locked(path && path[0] != '\0' ? path : NULL);
    pthread_mutex_unlock(&g_log_mutex);
    return result;
}

void log_shutdown(void) {
    pthread_mutex_lock(&g_log_mutex);
    if (g_log_file) {
        fflush(g_log_file);
        fclose(g_log_file);
        g_log_file = NULL;
    }
    pthread_mutex_unlock(&g_log_mutex);
}

void log_event_v(log_category_t category, const char* id, const char* fmt, va_list args) {
    pthread_mutex_lock(&g_log_mutex);

    if (!g_log_file && log_open_locked(NULL) != 0) {
        pthread_mutex_unlock(&g_log_mutex);
        return;
    }

    char timestamp[32];
    log_get_timestamp(timestamp, sizeof(timestamp));

    const char* safe_id = (id && id[0] != '\0') ? id : "N/A";
    const char* category_str = log_category_to_string(category);

    fprintf(g_log_file, "[%s] [%s] [%s] ", timestamp, safe_id, category_str);
    vfprintf(g_log_file, fmt, args);
    fputc('\n', g_log_file);
    fflush(g_log_file);

    pthread_mutex_unlock(&g_log_mutex);
}

void log_event(log_category_t category, const char* id, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    log_event_v(category, id, fmt, args);
    va_end(args);
}
