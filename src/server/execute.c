#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h> // for send()
#include "../../include/common.h"
#include "../../include/execute.h"

void execute_file(int client_sock, const char* filename) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", STORAGE_DIR, filename);

    FILE *fp = fopen(path, "r");
    if (!fp) {
        char msg[256];
        snprintf(msg, sizeof(msg), "ERROR: Cannot open file '%s'\n", filename);
        send(client_sock, msg, strlen(msg), 0);
        return;
    }

    char line[512];
    while (fgets(line, sizeof(line), fp)) {
        line[strcspn(line, "\n")] = 0; // remove newline
        if (strlen(line) == 0) continue;

        FILE *cmd_fp = popen(line, "r");
        if (!cmd_fp) {
            char err[1024];
            snprintf(err, sizeof(err), "ERROR: Failed to execute command: %s\n", line);
            send(client_sock, err, strlen(err), 0);
            continue;
        }

        char output[1024];
        while (fgets(output, sizeof(output), cmd_fp)) {
            send(client_sock, output, strlen(output), 0);
        }

        pclose(cmd_fp);
    }

    fclose(fp);
}
