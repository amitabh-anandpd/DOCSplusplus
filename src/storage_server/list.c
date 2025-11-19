// list.c
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#include "../include/common.h"  // adjust path if needed

// List all users from storage/users.txt
void list_users(int client_sock) {
    FILE *fp;
    char line[256];

    fp = fopen("storage/users.txt", "r");
    if (!fp) {
        const char *msg = "Error: Could not open users file.\n";
        send(client_sock, msg, strlen(msg), 0);
        return;
    }

    // Optional header
    const char *header = "Registered users:\n";
    send(client_sock, header, strlen(header), 0);

    while (fgets(line, sizeof(line), fp)) {
        // Remove newline
        line[strcspn(line, "\n")] = 0;

        // Skip comments or empty lines
        if (line[0] == '#' || line[0] == '\0')
            continue;

        // Split "username:password"
        char *colon = strchr(line, ':');
        if (!colon)
            continue;  // malformed line, skip

        *colon = '\0';  // terminate at username

        // Print username
        char out[300];
        snprintf(out, sizeof(out), "--> %s\n", line);
        send(client_sock, out, strlen(out), 0);
    }

    fclose(fp);
}
