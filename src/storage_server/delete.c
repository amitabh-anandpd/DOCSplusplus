#include "../../include/common.h"
#include "../../include/delete.h"
#include "../../include/acl.h"

int delete_from_storage(int client_sock, const char *filename, const char *username) {
    if (filename == NULL || filename[0] == '\0') {
        fprintf(stderr, "delete_from_storage: filename not provided\n");
        return -1;
    }

    // Check write access (only owner or those with write access can delete)
    if (!check_write_access(filename, username)) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Error: Access denied. You do not have permission to delete '%s'\n", filename);
        send(client_sock, msg, strlen(msg), 0);
        return -1;
    }

    char filepath[512];
    int written = snprintf(filepath, sizeof(filepath), "%s/storage%d/files/%s", STORAGE_DIR, get_storage_id(), filename);

    if (written < 0 || written >= (int)sizeof(filepath)) {
        fprintf(stderr, "delete_from_storage: path too long\n");
        return -1;
    }

    if (unlink(filepath) == -1) {
        perror("delete_from_storage: unlink failed");
        return -1;
    }
    
    // Also delete metadata file
    char metapath[512];
    snprintf(metapath, sizeof(metapath), "%s/storage%d/meta/%s.meta", STORAGE_DIR, get_storage_id(), filename);
    unlink(metapath); // Ignore errors
    
    char msg[256];
    snprintf(msg, sizeof(msg), "File '%s' deleted successfully\n", filename);
    send(client_sock, msg, strlen(msg), 0);
    return 0;
}
