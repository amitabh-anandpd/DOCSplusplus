#include "../../include/common.h"
#include "../../include/view.h"

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
    struct passwd *pwd;

    dir = opendir(STORAGE_DIR);
    if (!dir) {
        sprintf(response, "Error: Cannot open storage directory\n");
        send(client_sock, response, strlen(response), 0);
        return;
    }

    strcpy(response, "");

    // If showing long format, print the header
    if (show_long) {
        strcat(response, "---------------------------------------------------------\n");
        strcat(response, "|  Filename  | Words | Chars | Last Access Time | Owner |\n");
        strcat(response, "|------------|-------|-------|------------------|-------|\n");
    }

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

            char access_time[64];
            strftime(access_time, sizeof(access_time), "%Y-%m-%d %H:%M",
                     localtime(&file_stat.st_atime));

            // Get owner name
            pwd = getpwuid(file_stat.st_uid);
            const char *owner = pwd ? pwd->pw_name : "unknown";

            char line[512];
            sprintf(line, "| %-10s| %-5d | %-5d | %-16s | %-5s |\n",
                    entry->d_name, word_count, char_count, access_time, owner);
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