#include "logger.h"
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

void log_event(const char *level, const char *fmt, ...) {
    FILE *fp = fopen(LOG_FILE_PATH, "a");
    if (!fp) return;
    
    // Timestamp
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char timebuf[32];
    strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", tm_info);

    // Format message
    char msg[2048];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);

    // Compose log line
    char logline[2200];
    snprintf(logline, sizeof(logline), "[%s] [%s] %s\n", timebuf, level, msg);

    // Print to terminal
    fputs(logline, stdout);
    fflush(stdout);
    // Write to log file
    fputs(logline, fp);
    fclose(fp);
}
