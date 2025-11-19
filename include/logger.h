#ifndef LOGGER_H
#define LOGGER_H

#include <stdio.h>
#include <stdarg.h>
#include <time.h>

// Log levels
#define LOG_INFO    "INFO"
#define LOG_WARN    "WARN"
#define LOG_ERROR   "ERROR"

// Log file path (relative to base storage directory)
#define LOG_FILE_PATH "storage/nameserver.log"

void log_event(const char *level, const char *fmt, ...);

#endif // LOGGER_H
