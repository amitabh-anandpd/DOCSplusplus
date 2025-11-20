
#include "../../include/common.h"
#include "../../include/checkpoint.h"
#include "../../include/acl.h"
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>
#include <time.h>

#define MAX_PATH 1024
#define MAX_TAG 128
#define MAX_BUFFER 4096
#define MAX_FILENAME 256

// Function to ensure checkpoint directory exists for this storage server
static int ensure_checkpoint_dir(int storage_id) {
    char checkpoint_dir[MAX_PATH];
    snprintf(checkpoint_dir, MAX_PATH, "%s/storage%d/checkpoints", 
             STORAGE_DIR, storage_id);
    
    struct stat st = {0};
    if (stat(checkpoint_dir, &st) == -1) {
        if (mkdir(checkpoint_dir, 0700) == -1) {
            return -1;
        }
    }
    return 0;
}

// Function to sanitize filename for checkpoint naming
static void sanitize_filename(const char *input, char *output, size_t out_size) {
    size_t j = 0;
    // Limit input length to prevent overflow
    size_t max_input = (out_size - 1 < MAX_FILENAME) ? out_size - 1 : MAX_FILENAME;
    
    for (size_t i = 0; input[i] != '\0' && i < max_input && j < out_size - 1; i++) {
        if (input[i] == '/' || input[i] == '\\') {
            output[j++] = '_';
        } else {
            output[j++] = input[i];
        }
    }
    output[j] = '\0';
}

// Function to copy file content
static int copy_file(const char *src, const char *dest) {
    FILE *source = fopen(src, "rb");
    if (!source) {
        return -1;
    }

    FILE *destination = fopen(dest, "wb");
    if (!destination) {
        fclose(source);
        return -1;
    }

    char buffer[MAX_BUFFER];
    size_t bytes;
    
    while ((bytes = fread(buffer, 1, MAX_BUFFER, source)) > 0) {
        if (fwrite(buffer, 1, bytes, destination) != bytes) {
            fclose(source);
            fclose(destination);
            return -1;
        }
    }

    fclose(source);
    fclose(destination);
    return 0;
}

// Create a checkpoint
int checkpoint_create(int client_sock, const char *filename, const char *tag, 
                      const char *username, int storage_id) {
    char response[1024];
    
    // Check read access (need to read file to create checkpoint)
    if (!check_read_access(filename, username)) {
        snprintf(response, sizeof(response), 
                "Error: Access denied. You do not have read permission for '%s'\n", 
                filename);
        send(client_sock, response, strlen(response), 0);
        return -1;
    }
    
    // Construct file path
    char file_path[MAX_PATH];
    snprintf(file_path, MAX_PATH, "%s/storage%d/files/%s", 
             STORAGE_DIR, storage_id, filename);
    
    // Check if file exists
    struct stat st;
    if (stat(file_path, &st) == -1) {
        snprintf(response, sizeof(response), 
                "Error: File '%s' does not exist\n", filename);
        send(client_sock, response, strlen(response), 0);
        return -1;
    }

    // Ensure checkpoint directory exists
    if (ensure_checkpoint_dir(storage_id) == -1) {
        snprintf(response, sizeof(response), 
                "Error: Cannot create checkpoint directory\n");
        send(client_sock, response, strlen(response), 0);
        return -1;
    }

    // Create checkpoint filename
    char sanitized[MAX_FILENAME];
    sanitize_filename(filename, sanitized, MAX_FILENAME);
    
    char checkpoint_path[MAX_PATH];
    snprintf(checkpoint_path, MAX_PATH, "%s/storage%d/checkpoints/%s_%s.ckpt", 
             STORAGE_DIR, storage_id, sanitized, tag);

    // Check if checkpoint already exists
    if (stat(checkpoint_path, &st) == 0) {
        snprintf(response, sizeof(response), 
                "Error: Checkpoint '%s' already exists for file '%s'\n", 
                tag, filename);
        send(client_sock, response, strlen(response), 0);
        return -1;
    }

    // Copy file to checkpoint
    if (copy_file(file_path, checkpoint_path) == -1) {
        snprintf(response, sizeof(response), 
                "Error: Failed to create checkpoint\n");
        send(client_sock, response, strlen(response), 0);
        return -1;
    }

    // Save metadata
    char meta_path[MAX_PATH];
    snprintf(meta_path, MAX_PATH, "%s/storage%d/checkpoints/%s_%s.meta", 
             STORAGE_DIR, storage_id, sanitized, tag);
    
    FILE *meta = fopen(meta_path, "w");
    if (!meta) {
        remove(checkpoint_path);
        snprintf(response, sizeof(response), 
                "Error: Cannot create metadata file\n");
        send(client_sock, response, strlen(response), 0);
        return -1;
    }

    time_t now = time(NULL);
    fprintf(meta, "filename=%s\n", filename);
    fprintf(meta, "tag=%s\n", tag);
    fprintf(meta, "timestamp=%ld\n", (long)now);
    fprintf(meta, "created_by=%s\n", username);
    fclose(meta);

    snprintf(response, sizeof(response), 
            "Success: Checkpoint '%s' created successfully for file '%s'\n", 
            tag, filename);
    send(client_sock, response, strlen(response), 0);
    return 0;
}

// View checkpoint content
int checkpoint_view(int client_sock, const char *filename, const char *tag, 
                   const char *username, int storage_id) {
    char response[MAX_BUFFER];
    
    // Check read access
    if (!check_read_access(filename, username)) {
        snprintf(response, sizeof(response), 
                "Error: Access denied. You do not have read permission for '%s'\n", 
                filename);
        send(client_sock, response, strlen(response), 0);
        return -1;
    }
    
    char sanitized[MAX_FILENAME];
    sanitize_filename(filename, sanitized, MAX_FILENAME);
    
    char checkpoint_path[MAX_PATH];
    snprintf(checkpoint_path, MAX_PATH, "%s/storage%d/checkpoints/%s_%s.ckpt", 
             STORAGE_DIR, storage_id, sanitized, tag);

    // Check if checkpoint exists
    struct stat st;
    if (stat(checkpoint_path, &st) == -1) {
        snprintf(response, sizeof(response), 
                "Error: Checkpoint '%s' not found for file '%s'\n", 
                tag, filename);
        send(client_sock, response, strlen(response), 0);
        return -1;
    }

    FILE *ckpt = fopen(checkpoint_path, "r");
    if (!ckpt) {
        snprintf(response, sizeof(response), 
                "Error: Cannot open checkpoint file\n");
        send(client_sock, response, strlen(response), 0);
        return -1;
    }

    // Send header
    snprintf(response, sizeof(response), 
            "=== Content of checkpoint '%s' for file '%s' ===\n", 
            tag, filename);
    send(client_sock, response, strlen(response), 0);
    
    // Send file content in chunks
    char buffer[MAX_BUFFER];
    size_t bytes_read;
    while ((bytes_read = fread(buffer, 1, sizeof(buffer) - 1, ckpt)) > 0) {
        buffer[bytes_read] = '\0';
        send(client_sock, buffer, bytes_read, 0);
    }
    
    fclose(ckpt);
    
    // Send footer
    snprintf(response, sizeof(response), "\n=== End of checkpoint ===\n");
    send(client_sock, response, strlen(response), 0);
    
    return 0;
}

// Revert to a checkpoint
int checkpoint_revert(int client_sock, const char *filename, const char *tag, 
                     const char *username, int storage_id) {
    char response[1024];
    
    // Check write access (need to modify file)
    if (!check_write_access(filename, username)) {
        snprintf(response, sizeof(response), 
                "Error: Access denied. You do not have write permission for '%s'\n", 
                filename);
        send(client_sock, response, strlen(response), 0);
        return -1;
    }
    
    char sanitized[MAX_FILENAME];
    sanitize_filename(filename, sanitized, MAX_FILENAME);
    
    char checkpoint_path[MAX_PATH];
    snprintf(checkpoint_path, MAX_PATH, "%s/storage%d/checkpoints/%s_%s.ckpt", 
             STORAGE_DIR, storage_id, sanitized, tag);

    // Check if checkpoint exists
    struct stat st;
    if (stat(checkpoint_path, &st) == -1) {
        snprintf(response, sizeof(response), 
                "Error: Checkpoint '%s' not found for file '%s'\n", 
                tag, filename);
        send(client_sock, response, strlen(response), 0);
        return -1;
    }

    // Construct file path
    char file_path[MAX_PATH];
    snprintf(file_path, MAX_PATH, "%s/storage%d/files/%s", 
             STORAGE_DIR, storage_id, filename);

    // Create backup of current file (optional, for safety)
    char backup_path[MAX_PATH];
    snprintf(backup_path, MAX_PATH, "%s/storage%d/files/%s.backup",
             STORAGE_DIR, storage_id, filename);
    
    if (stat(file_path, &st) == 0) {
        copy_file(file_path, backup_path);
    }

    // Restore from checkpoint
    if (copy_file(checkpoint_path, file_path) == -1) {
        snprintf(response, sizeof(response), 
                "Error: Failed to restore from checkpoint\n");
        send(client_sock, response, strlen(response), 0);
        // Try to restore backup
        if (stat(backup_path, &st) == 0) {
            copy_file(backup_path, file_path);
        }
        return -1;
    }

    snprintf(response, sizeof(response), 
            "Success: File '%s' successfully reverted to checkpoint '%s'\n", 
            filename, tag);
    send(client_sock, response, strlen(response), 0);
    
    // Remove backup on success
    remove(backup_path);
    return 0;
}

// List all checkpoints for a file
int checkpoint_list(int client_sock, const char *filename, 
                   const char *username, int storage_id) {
    char response[MAX_BUFFER];
    
    // Check read access
    if (!check_read_access(filename, username)) {
        snprintf(response, sizeof(response), 
                "Error: Access denied. You do not have read permission for '%s'\n", 
                filename);
        send(client_sock, response, strlen(response), 0);
        return -1;
    }
    
    char sanitized[MAX_FILENAME];
    sanitize_filename(filename, sanitized, MAX_FILENAME);

    char checkpoint_dir[MAX_PATH];
    snprintf(checkpoint_dir, MAX_PATH, "%s/storage%d/checkpoints", 
             STORAGE_DIR, storage_id);

    DIR *dir = opendir(checkpoint_dir);
    if (!dir) {
        snprintf(response, sizeof(response), 
                "No checkpoints found for file '%s'\n", filename);
        send(client_sock, response, strlen(response), 0);
        return 0;
    }

    // Send header
    snprintf(response, sizeof(response), 
            "Checkpoints for file '%s':\n", filename);
    send(client_sock, response, strlen(response), 0);
    
    snprintf(response, sizeof(response), 
            "%-20s %-30s %-15s %s\n", 
            "Tag", "Timestamp", "Size", "Created By");
    send(client_sock, response, strlen(response), 0);
    
    snprintf(response, sizeof(response), 
            "------------------------------------------------------------------------\n");
    send(client_sock, response, strlen(response), 0);

    struct dirent *entry;
    int count = 0;
    char prefix[MAX_FILENAME + 2];
    snprintf(prefix, sizeof(prefix), "%s_", sanitized);
    size_t prefix_len = strlen(prefix);

    while ((entry = readdir(dir)) != NULL) {
        // Check if entry matches our file and is a checkpoint file
        if (strncmp(entry->d_name, prefix, prefix_len) == 0 && 
            strstr(entry->d_name, ".ckpt") != NULL) {
            
            // Extract tag
            char tag[MAX_TAG];
            const char *tag_start = entry->d_name + prefix_len;
            const char *tag_end = strstr(tag_start, ".ckpt");
            size_t tag_len = tag_end - tag_start;
            strncpy(tag, tag_start, tag_len);
            tag[tag_len] = '\0';

            // Get checkpoint file stats
            char ckpt_path[MAX_PATH];
            

            int written = snprintf(
                ckpt_path,
                sizeof(ckpt_path),
                "%s/%s",
                checkpoint_dir,
                entry->d_name
            );

            if (written < 0 || written >= (int)sizeof(ckpt_path)) {
                // Path got truncated or snprintf failed; skip this entry
                continue;
            }
            
            struct stat st;
            if (stat(ckpt_path, &st) == 0) {
                // Try to read metadata
                char meta_path[MAX_PATH];
                snprintf(meta_path, MAX_PATH, "%s/storage%d/checkpoints/%s_%s.meta", 
                        STORAGE_DIR, storage_id, sanitized, tag);
                
                char timestamp_str[64] = "N/A";
                char created_by[64] = "Unknown";
                
                FILE *meta = fopen(meta_path, "r");
                if (meta) {
                    char line[256];
                    while (fgets(line, sizeof(line), meta)) {
                        if (strncmp(line, "timestamp=", 10) == 0) {
                            long ts = atol(line + 10);
                            time_t timestamp = (time_t)ts;
                            struct tm *tm_info = localtime(&timestamp);
                            strftime(timestamp_str, sizeof(timestamp_str), 
                                   "%Y-%m-%d %H:%M:%S", tm_info);
                        }
                        else if (strncmp(line, "created_by=", 11) == 0) {
                            strncpy(created_by, line + 11, sizeof(created_by) - 1);
                            created_by[strcspn(created_by, "\n")] = 0;
                        }
                    }
                    fclose(meta);
                }

                snprintf(response, sizeof(response), 
                        "%-20s %-30s %-15ld %s\n", 
                        tag, timestamp_str, (long)st.st_size, created_by);
                send(client_sock, response, strlen(response), 0);
                count++;
            }
        }
    }

    closedir(dir);

    if (count == 0) {
        snprintf(response, sizeof(response), 
                "No checkpoints found for this file\n");
        send(client_sock, response, strlen(response), 0);
    } else {
        snprintf(response, sizeof(response), 
                "\nTotal: %d checkpoint(s)\n", count);
        send(client_sock, response, strlen(response), 0);
    }

    return 0;
}