#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 500
#endif

#include "../../include/common.h"
#include "../../include/acl.h"
#include <unistd.h>
#include <time.h>  // for nanosleep()

void stream_file(int client_sock, const char *filename, const char *username) {
    // Check read access
    if (!check_read_access(filename, username)) {
        char msg[256];
        sprintf(msg, "ERROR: Access denied. You do not have permission to stream '%s'.\n", filename);
        send(client_sock, msg, strlen(msg), 0);
        return;
    }
    
    char path[512];
    sprintf(path, "%s/storage%d/files/%s", STORAGE_DIR, get_storage_id(), filename);

    FILE *fp = fopen(path, "r");
    if (!fp) {
        char msg[256];
        sprintf(msg, "ERROR: Cannot open file '%s'\n", filename);
        send(client_sock, msg, strlen(msg), 0);
        return;
    }

    char word[252], buffer[256];
    while (fscanf(fp, "%251s", word) == 1) {
        int len = snprintf(buffer, sizeof(buffer), "%s ", word);
        if (len < 0 || len >= (int)sizeof(buffer)) continue;

        if (send(client_sock, buffer, strlen(buffer), 0) <= 0) {
            printf("Client disconnected during stream.\n");
            break;
        }

        // ✅ 0.1 second delay (nanosleep = POSIX safe)
        struct timespec ts = {0, 100000000L};
        nanosleep(&ts, NULL);
    }

    fclose(fp);

    char done[] = "\n--- End of Stream ---\n";
    send(client_sock, done, strlen(done), 0);
}
