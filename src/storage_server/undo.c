#include "../../include/common.h"
#include "../../include/acl.h"
#include <sys/stat.h>

void undo_last_change(int client_sock, const char* filename, const char* username) {
    char response[512];
    char current_path[512];
    char undo_path[512];
    char temp_path[512];
    
    // Check write access
    if (!check_write_access(filename, username)) {
        sprintf(response, "Error: Access denied. You do not have write permission for '%s'\n", filename);
        send(client_sock, response, strlen(response), 0);
        return;
    }
    
    // Construct paths
    sprintf(current_path, "%s/storage%d/files/%s", STORAGE_DIR, get_storage_id(), filename);
    sprintf(undo_path, "%s/storage%d/undo/%s", STORAGE_DIR, get_storage_id(), filename);
    sprintf(temp_path, "%s/storage%d/swap/%s.tmp", STORAGE_DIR, get_storage_id(), filename);
    
    // Check if current file exists
    FILE *current_fp = fopen(current_path, "r");
    if (!current_fp) {
        sprintf(response, "Error: File '%s' not found\n", filename);
        send(client_sock, response, strlen(response), 0);
        return;
    }
    fclose(current_fp);
    
    // Check if undo backup exists
    FILE *undo_fp = fopen(undo_path, "r");
    if (!undo_fp) {
        sprintf(response, "Error: No undo history available for '%s'\n", filename);
        send(client_sock, response, strlen(response), 0);
        return;
    }
    fclose(undo_fp);
    
    // Step 1: Copy current file to temp (this becomes the new undo backup)
    FILE *src = fopen(current_path, "r");
    FILE *dst = fopen(temp_path, "w");
    if (!src || !dst) {
        if (src) fclose(src);
        if (dst) fclose(dst);
        sprintf(response, "Error: Failed to create temporary backup\n");
        send(client_sock, response, strlen(response), 0);
        return;
    }
    
    char buffer[4096];
    size_t bytes;
    while ((bytes = fread(buffer, 1, sizeof(buffer), src)) > 0) {
        fwrite(buffer, 1, bytes, dst);
    }
    fclose(src);
    fclose(dst);
    
    // Step 2: Copy undo backup to current file (restore previous version)
    src = fopen(undo_path, "r");
    dst = fopen(current_path, "w");
    if (!src || !dst) {
        if (src) fclose(src);
        if (dst) fclose(dst);
        sprintf(response, "Error: Failed to restore from undo backup\n");
        send(client_sock, response, strlen(response), 0);
        return;
    }
    
    while ((bytes = fread(buffer, 1, sizeof(buffer), src)) > 0) {
        fwrite(buffer, 1, bytes, dst);
    }
    fclose(src);
    fclose(dst);
    
    // Step 3: Move temp to undo (swap the backups)
    remove(undo_path);
    rename(temp_path, undo_path);
    
    // Update metadata - last modified time
    FileMetadata meta;
    if (read_metadata_file(filename, &meta) == 0) {
        meta.last_modified = time(NULL);
        update_metadata_file(filename, &meta);
    }
    
    sprintf(response, "Undo Successful!\n");
    send(client_sock, response, strlen(response), 0);
}