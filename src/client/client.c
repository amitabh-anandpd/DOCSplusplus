#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <strings.h>
#include "../../include/common.h"
#include "../../include/client_write.h"

int main() {
    int sock;
    struct sockaddr_in server_addr;
    char buffer[2048], command[100];
    int target_port;
    // Print quick help once
    printf("Commands:\n"
        " VIEW | VIEW -a | VIEW -l | VIEW -al\n"
        " READ <filename> | CREATE <filename> | DELETE <filename>\n"
        " WRITE <filename> <sentence number> | INFO <filename> | STREAM <filename> | EXEC <filename>\n"
        " Type EXIT or QUIT to leave the client.\n\n");

    while (1) {
        printf("Client: ");
        fflush(stdout);

        if (!fgets(command, sizeof(command), stdin)) {
            // EOF (Ctrl+D)
            printf("\n");
            break;
        }

        // remove newline
        command[strcspn(command, "\n")] = 0;

        // skip empty
        if (strlen(command) == 0) continue;

        // exit commands
        if (strcasecmp(command, "EXIT") == 0 || strcasecmp(command, "QUIT") == 0) break;

        // Decide where to connect: STREAM should go straight to storage server
        if (strncmp(command, "STREAM", 6) == 0) {
            target_port = STORAGE_SERVER_PORT;
        } else {
            target_port = NAME_SERVER_PORT;
        }

        sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) {
            perror("Socket creation failed");
            continue;
        }

        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(target_port);
        server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

        if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
            perror("Connection failed");
            close(sock);
            continue;
        }

        send(sock, command, strlen(command), 0);

        // If STREAM we read directly from storage server
        if (strncmp(command, "STREAM", 6) == 0) {
            printf("\n--- Streaming Content ---\n");

            ssize_t bytes;
            while ((bytes = read(sock, buffer, sizeof(buffer) - 1)) > 0) {
                buffer[bytes] = '\0';
                printf("%s", buffer);
                fflush(stdout);
            }

            if (bytes == 0)
                printf("\n\n[INFO] Stream ended successfully.\n");
            else
                printf("\n\n[ERROR] Connection lost while streaming (server may have gone down).\n");
        }
        else if(strncmp(command, "WRITE", 5) == 0) {
            char filename[256];
            int sentence_num;
            sscanf(command + 6, "%s %d", filename, &sentence_num);
            if (strlen(filename) == 0) {
                printf("Error: Please specify a filename\n");
            } else {
                client_write(sock, filename, sentence_num);
            }
        }
        else {
            // For all other commands: read until server closes the connection
            printf("\n--- Server Response ---\n");
            ssize_t bytes;
            while ((bytes = read(sock, buffer, sizeof(buffer) - 1)) > 0) {
                buffer[bytes] = '\0';
                printf("%s", buffer);
            }

            if (bytes == 0) {
                printf("\n[INFO] Response complete.\n");
            } else {
                printf("\n[ERROR] Connection closed unexpectedly while reading response.\n");
            }
        }

        close(sock);
    }

    return 0;
}
