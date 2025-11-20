#include "../../include/common.h"
#include "../../include/view.h"
#include "../../include/acl.h"  // ADD THIS - to use check_read_access()

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
// MODIFY THIS FUNCTION - add username parameter
void list_files(int client_sock, int show_all, int show_long, const char* username) {
    DIR *dir;
    struct dirent *entry;
    struct stat file_stat;
    char path[512], response[8192];
    
    // Open per-server files directory
    char server_files_dir[512];
    sprintf(server_files_dir, "%s/storage%d/files", STORAGE_DIR, get_storage_id());
    
    dir = opendir(server_files_dir);
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
    
    int file_count = 0;  // Track how many files user can see
    
    while ((entry = readdir(dir)) != NULL) {
        // Skip . and ..
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        
        // ADD THIS: Check if user has read access to this file
        if (!show_all && !check_read_access(entry->d_name, username)) {
            continue;  // Skip files user doesn't have access to
        }
        
        sprintf(path, "%s/storage%d/files/%s", STORAGE_DIR, get_storage_id(), entry->d_name);
        if (stat(path, &file_stat) != 0) {
            continue;
        }
        
        if (show_long) {
            int word_count = 0, char_count = 0;
            count_file(path, &word_count, &char_count);

            // NEW: pull owner & last access from metadata file instead of filesystem user
            FileMetadata meta;
            const char *owner = "unknown";
            time_t last_access_raw = file_stat.st_atime;

            if (read_metadata_file(entry->d_name, &meta) == 0) {
                if (meta.owner[0] != '\0') owner = meta.owner;
                if (meta.last_accessed > 0) last_access_raw = meta.last_accessed;
            }
            
            char access_time[64];
            strftime(access_time, sizeof(access_time), "%Y-%m-%d %H:%M",
                     localtime(&last_access_raw));

            char line[512];
            sprintf(line, "| %-10s| %-5d | %-5d | %-16s | %-10s |\n",
                    entry->d_name, word_count, char_count, access_time, owner);
            strcat(response, line);
        } else {
            strcat(response, entry->d_name);
            strcat(response, "\n");
        }
        
        file_count++;
    }
    
    closedir(dir);
    
    if (file_count == 0) {
        strcpy(response, "(no files found or no access)\n");
    }
    
    send(client_sock, response, strlen(response), 0);
}