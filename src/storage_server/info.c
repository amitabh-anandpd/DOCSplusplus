#include "../../include/common.h"
#include "../../include/info.h"
#include "../../include/acl.h"

// Helper: convert mode to rwx string (like ls -l)
void get_permissions_string(mode_t mode, char *perm_str) {
    perm_str[0] = (mode & S_IRUSR) ? 'r' : '-';
    perm_str[1] = (mode & S_IWUSR) ? 'w' : '-';
    perm_str[2] = (mode & S_IXUSR) ? 'x' : '-';
    perm_str[3] = (mode & S_IRGRP) ? 'r' : '-';
    perm_str[4] = (mode & S_IWGRP) ? 'w' : '-';
    perm_str[5] = (mode & S_IXGRP) ? 'x' : '-';
    perm_str[6] = (mode & S_IROTH) ? 'r' : '-';
    perm_str[7] = (mode & S_IWOTH) ? 'w' : '-';
    perm_str[8] = (mode & S_IXOTH) ? 'x' : '-';
    perm_str[9] = '\0';
}

void file_info(int client_sock, const char *filename, const char *username) {
    // Check read access
    if (!check_read_access(filename, username)) {
        char msg[256];
        sprintf(msg, "ERROR: Access denied. You do not have permission to view info for '%s'.\n", filename);
        send(client_sock, msg, strlen(msg), 0);
        return;
    }
    
    char path[512];
    sprintf(path, "%s/storage%d/files/%s", STORAGE_DIR, get_storage_id(), filename);
    struct stat st;

    // Check if file exists
    if (stat(path, &st) != 0) {
        char err[256];
        sprintf(err, "ERROR: File '%s' not found or inaccessible.\n", filename);
        send(client_sock, err, strlen(err), 0);
        return;
    }

    // Read metadata
    FileMetadata meta;
    char owner_str[128] = "unknown";
    char created_str[64] = "N/A";
    char read_users_str[512] = "N/A";
    char write_users_str[512] = "N/A";
    
    if (read_metadata_file(filename, &meta) == 0) {
        strncpy(owner_str, meta.owner, sizeof(owner_str) - 1);
        strftime(created_str, sizeof(created_str), "%Y-%m-%d %H:%M:%S", localtime(&meta.created_time));
        strncpy(read_users_str, meta.read_users, sizeof(read_users_str) - 1);
        strncpy(write_users_str, meta.write_users, sizeof(write_users_str) - 1);
    }

    char response[2048];
    char perm_str[10];
    get_permissions_string(st.st_mode, perm_str);

    // Convert timestamps
    char atime[64], mtime[64];
    strftime(atime, sizeof(atime), "%Y-%m-%d %H:%M:%S", localtime(&st.st_atime));
    strftime(mtime, sizeof(mtime), "%Y-%m-%d %H:%M:%S", localtime(&st.st_mtime));

    sprintf(response,
        "------------------- FILE INFO -------------------\n"
        "File Name      : %s\n"
        "File Size      : %ld bytes\n"
        "Owner          : %s\n"
        "Permissions    : %s\n"
        "Created        : %s\n"
        "Last Modified  : %s\n"
        "Last Access    : %s\n"
        "Read Access    : %s\n"
        "Write Access   : %s\n"
        "-------------------------------------------------\n",
        filename,
        st.st_size,
        owner_str,
        perm_str,
        created_str,
        mtime,
        atime,
        read_users_str,
        write_users_str
    );

    send(client_sock, response, strlen(response), 0);
}
