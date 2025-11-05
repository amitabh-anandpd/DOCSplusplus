#include "../../include/common.h"
#include "../../include/delete.h"

int delete_from_storage(int client_sock, const char *filename) {
    if (filename == NULL || filename[0] == '\0') {
        fprintf(stderr, "delete_from_storage: filename not provided\n");
        return -1;
    }

    char filepath[512];
    int written = snprintf(filepath, sizeof(filepath), "%s/%s", STORAGE_DIR, filename);

    if (written < 0 || written >= (int)sizeof(filepath)) {
        fprintf(stderr, "delete_from_storage: path too long\n");
        return -1;
    }

    if (unlink(filepath) == -1) {
        perror("delete_from_storage: unlink failed");
        return -1;
    }
    char msg[256];
    snprintf(msg, sizeof(msg), "File '%s' deleted successfully\n", filename);
    send(client_sock, msg, strlen(msg), 0);
    return 0;
}
