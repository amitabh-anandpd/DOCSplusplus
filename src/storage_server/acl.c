#include "../include/acl.h"
#include "../include/common.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

extern int get_storage_id(void);

// Create metadata file for a new file
int create_metadata_file(const char *filename, const char *owner) {
    char meta_path[512];
    snprintf(meta_path, sizeof(meta_path), "%s/storage%d/meta/%s.meta", 
             STORAGE_DIR, get_storage_id(), filename);
    
    FILE *fp = fopen(meta_path, "w");
    if (!fp) return -1;
    
    time_t now = time(NULL);
    fprintf(fp, "OWNER:%s\n", owner);
    fprintf(fp, "CREATED:%ld\n", (long)now);
    fprintf(fp, "LAST_ACCESS:%ld\n", (long)now);
    fprintf(fp, "READ_USERS:%s\n", owner);  // Owner has read access by default
    fprintf(fp, "WRITE_USERS:%s\n", owner); // Owner has write access by default
    
    fclose(fp);
    return 0;
}

// Read metadata from file
int read_metadata_file(const char *filename, FileMetadata *meta) {
    char meta_path[512];
    snprintf(meta_path, sizeof(meta_path), "%s/storage%d/meta/%s.meta", 
             STORAGE_DIR, get_storage_id(), filename);
    
    FILE *fp = fopen(meta_path, "r");
    if (!fp) return -1;
    
    char line[1024];
    memset(meta, 0, sizeof(FileMetadata));
    
    while (fgets(line, sizeof(line), fp)) {
        line[strcspn(line, "\n")] = 0;
        
        if (strncmp(line, "OWNER:", 6) == 0) {
            strncpy(meta->owner, line + 6, sizeof(meta->owner) - 1);
        } else if (strncmp(line, "CREATED:", 8) == 0) {
            meta->created_time = (time_t)atol(line + 8);
        } else if (strncmp(line, "LAST_ACCESS:", 12) == 0) {
            meta->last_accessed = (time_t)atol(line + 12);
        } else if (strncmp(line, "READ_USERS:", 11) == 0) {
            strncpy(meta->read_users, line + 11, sizeof(meta->read_users) - 1);
        } else if (strncmp(line, "WRITE_USERS:", 12) == 0) {
            strncpy(meta->write_users, line + 12, sizeof(meta->write_users) - 1);
        }
    }
    
    fclose(fp);
    return 0;
}

// Update metadata file
int update_metadata_file(const char *filename, FileMetadata *meta) {
    char meta_path[512];
    snprintf(meta_path, sizeof(meta_path), "%s/storage%d/meta/%s.meta", 
             STORAGE_DIR, get_storage_id(), filename);
    
    FILE *fp = fopen(meta_path, "w");
    if (!fp) return -1;
    
    fprintf(fp, "OWNER:%s\n", meta->owner);
    fprintf(fp, "CREATED:%ld\n", (long)meta->created_time);
    fprintf(fp, "LAST_ACCESS:%ld\n", (long)meta->last_accessed);
    fprintf(fp, "READ_USERS:%s\n", meta->read_users);
    fprintf(fp, "WRITE_USERS:%s\n", meta->write_users);
    
    fclose(fp);
    return 0;
}

// Helper function to check if a user is in a comma-separated list
static int user_in_list(const char *list, const char *username) {
    if (!list || !username) return 0;
    
    char list_copy[512];
    strncpy(list_copy, list, sizeof(list_copy) - 1);
    list_copy[sizeof(list_copy) - 1] = '\0';
    
    char *saveptr = NULL;
    char *token = strtok_r(list_copy, ",", &saveptr);
    while (token) {
        // Trim whitespace
        while (*token == ' ' || *token == '\t') token++;
        char *end = token + strlen(token) - 1;
        while (end > token && (*end == ' ' || *end == '\t')) {
            *end = '\0';
            end--;
        }
        
        if (strcmp(token, username) == 0) {
            return 1;
        }
        token = strtok_r(NULL, ",", &saveptr);
    }
    return 0;
}

// Check if user has read access
int check_read_access(const char *filename, const char *username) {
    FileMetadata meta;
    if (read_metadata_file(filename, &meta) < 0) {
        return 0; // No metadata = no access
    }
    
    // Owner always has access
    if (strcmp(meta.owner, username) == 0) return 1;
    
    // Check read_users list
    return user_in_list(meta.read_users, username);
}

// Check if user has write access
int check_write_access(const char *filename, const char *username) {
    FileMetadata meta;
    if (read_metadata_file(filename, &meta) < 0) {
        return 0; // No metadata = no access
    }
    
    // Owner always has access
    if (strcmp(meta.owner, username) == 0) return 1;
    
    // Check write_users list
    return user_in_list(meta.write_users, username);
}

// Add user to a list (helper function)
static void add_user_to_list(char *list, size_t list_size, const char *username) {
    // Check if user already in list
    if (user_in_list(list, username)) return;
    
    // Add user to list
    if (strlen(list) > 0) {
        strncat(list, ",", list_size - strlen(list) - 1);
    }
    strncat(list, username, list_size - strlen(list) - 1);
}

// Remove user from a list (helper function)
static void remove_user_from_list(char *list, size_t list_size, const char *username) {
    char new_list[512] = "";
    char list_copy[512];
    strncpy(list_copy, list, sizeof(list_copy) - 1);
    list_copy[sizeof(list_copy) - 1] = '\0';
    
    char *saveptr = NULL;
    char *token = strtok_r(list_copy, ",", &saveptr);
    int first = 1;
    while (token) {
        // Trim whitespace
        while (*token == ' ' || *token == '\t') token++;
        char *end = token + strlen(token) - 1;
        while (end > token && (*end == ' ' || *end == '\t')) {
            *end = '\0';
            end--;
        }
        
        if (strcmp(token, username) != 0) {
            if (!first) strcat(new_list, ",");
            strcat(new_list, token);
            first = 0;
        }
        token = strtok_r(NULL, ",", &saveptr);
    }
    
    strncpy(list, new_list, list_size - 1);
    list[list_size - 1] = '\0';
}

// Add read access for a user
int add_read_access(const char *filename, const char *username) {
    FileMetadata meta;
    if (read_metadata_file(filename, &meta) < 0) return -1;
    
    add_user_to_list(meta.read_users, sizeof(meta.read_users), username);
    
    return update_metadata_file(filename, &meta);
}

// Add write access for a user
int add_write_access(const char *filename, const char *username) {
    FileMetadata meta;
    if (read_metadata_file(filename, &meta) < 0) return -1;
    
    add_user_to_list(meta.write_users, sizeof(meta.write_users), username);
    
    return update_metadata_file(filename, &meta);
}

// Remove all access for a user
int remove_all_access(const char *filename, const char *username) {
    FileMetadata meta;
    if (read_metadata_file(filename, &meta) < 0) return -1;
    
    // Don't allow removing owner's access
    if (strcmp(meta.owner, username) == 0) return -2;
    
    remove_user_from_list(meta.read_users, sizeof(meta.read_users), username);
    remove_user_from_list(meta.write_users, sizeof(meta.write_users), username);
    
    return update_metadata_file(filename, &meta);
}
