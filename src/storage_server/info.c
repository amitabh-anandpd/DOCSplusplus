#include "../../include/common.h"
#include "../../include/info.h"

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

void file_info(int client_sock, const char *filename) {
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

    char response[1024];
    struct passwd *pwd = getpwuid(st.st_uid);
    const char *owner = pwd ? pwd->pw_name : "unknown";

    char perm_str[10];
    get_permissions_string(st.st_mode, perm_str);

    // Convert timestamps
    char atime[64], mtime[64], ctime_str[64];
    strftime(atime, sizeof(atime), "%Y-%m-%d %H:%M:%S", localtime(&st.st_atime));
    strftime(mtime, sizeof(mtime), "%Y-%m-%d %H:%M:%S", localtime(&st.st_mtime));
    strftime(ctime_str, sizeof(ctime_str), "%Y-%m-%d %H:%M:%S", localtime(&st.st_ctime));

    sprintf(response,
        "------------------- FILE INFO -------------------\n"
        "File Name   : %s\n"
        "File Size   : %ld bytes\n"
        "Owner       : %s\n"
        "Permissions : %s\n"
        "Created     : %s\n"
        "Last Modified: %s\n"
        "Last Access : %s\n"
        "-------------------------------------------------\n",
        filename,
        st.st_size,
        owner,
        perm_str,
        ctime_str,
        mtime,
        atime
    );

    send(client_sock, response, strlen(response), 0);
}
