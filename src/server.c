#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <dirent.h>
#include <sys/stat.h>
#include <time.h>

#define PORT 8080
#define STORAGE_DIR "./storage"

// Function to count words and characters in a file
void count_file(const char* path, int* words, int* chars) {
    FILE *fp = fopen(path, "r");
    if (!fp) {
        *words = 0;
        *chars = 0;
        return;
    }

    int c;
    int in_word = 0;
    *words = 0;
    *chars = 0;

    while ((c = fgetc(fp)) != EOF) {
        (*chars)++;
        if (c == ' ' || c == '\n' || c == '\t') {
            in_word = 0;
        } else if (!in_word) {
            in_word = 1;
            (*words)++;
        }
    }

    fclose(fp);
}

// Function to list files
void list_files(int client_sock, int show_all, int show_long) {
    DIR *dir;
    struct dirent *entry;
    struct stat file_stat;
    char path[512], response[8192]; // larger buffer

    dir = opendir(STORAGE_DIR);
    if (!dir) {
        sprintf(response, "Error: Cannot open storage directory\n");
        send(client_sock, response, strlen(response), 0);
        return;
    }

    strcpy(response, "");

    while ((entry = readdir(dir)) != NULL) {
        // Skip . and ..
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        // TODO: filter based on user access if show_all == 0 (currently always listing all)

        sprintf(path, "%s/%s", STORAGE_DIR, entry->d_name);
        if (stat(path, &file_stat) != 0) {
            continue; // Skip if stat fails
        }

        if (show_long) {
            int word_count = 0, char_count = 0;
            count_file(path, &word_count, &char_count);

            char mod_time[64];
            strftime(mod_time, sizeof(mod_time), "%Y-%m-%d %H:%M:%S",
                     localtime(&file_stat.st_mtime));

            char line[512];
            sprintf(line, "%-20s Words: %-5d Chars: %-5d Last Modified: %s\n",
                    entry->d_name, word_count, char_count, mod_time);
            strcat(response, line);
        } else {
            strcat(response, entry->d_name);
            strcat(response, "\n");
        }
    }

    closedir(dir);

    if (strlen(response) == 0)
        strcpy(response, "(no files found)\n");

    send(client_sock, response, strlen(response), 0);
}

int main() {
    int server_fd, client_sock;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_len = sizeof(client_addr);
    char buffer[1024];

    // Create socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("Socket creation failed");
        exit(1);
    }

    // Allow immediate reuse of the port
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt failed");
        exit(1);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        close(server_fd);
        exit(1);
    }

    listen(server_fd, 5);
    printf("Server started. Listening on port %d...\n", PORT);

    while (1) {
        client_sock = accept(server_fd, (struct sockaddr*)&client_addr, &addr_len);
        if (client_sock < 0) {
            perror("Accept failed");
            continue;
        }

        bzero(buffer, sizeof(buffer));
        read(client_sock, buffer, sizeof(buffer));

        // Remove newline / carriage return
        buffer[strcspn(buffer, "\n")] = 0;
        buffer[strcspn(buffer, "\r")] = 0;

        printf("Command received: '%s'\n", buffer);
        //printf("Buffer bytes: ");
        // for (int i = 0; i < strlen(buffer); i++) {
        //     printf("[%c:%d] ", buffer[i], buffer[i]);
        // }
        // printf("\n");
        // fflush(stdout);

        if (strncmp(buffer, "VIEW", 4) == 0) {
            // Parse flags from the command
            // Check for -a flag (can be standalone "-a" or part of "-al" or "-la")
            int show_all = (strstr(buffer, "-a") != NULL) || (strstr(buffer, "-la") != NULL);
            // Check for -l flag (can be standalone "-l" or part of "-al" or "-la")
            int show_long = (strstr(buffer, "-l") != NULL) || (strstr(buffer, "-al") != NULL) || (strstr(buffer, "-la") != NULL);
            
            //printf("Parsed flags: show_all=%d, show_long=%d\n", show_all, show_long);
            //fflush(stdout);
            
            list_files(client_sock, show_all, show_long);
        } else {
            char msg[] = "Invalid command.\n";
            send(client_sock, msg, strlen(msg), 0);
        }

        close(client_sock);
    }

    close(server_fd);
    return 0;
}