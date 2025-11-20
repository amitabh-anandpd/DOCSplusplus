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
    char response[32768];
    response[0] = '\0';

    char files_dir[PATH_MAX];
    snprintf(files_dir, sizeof(files_dir), "%s/storage%d/files", STORAGE_DIR, get_storage_id());
    DIR *dir = opendir(files_dir);
    if (!dir) {
        snprintf(response, sizeof(response), "ERROR: Cannot open files directory.\n");
        send(client_sock, response, strlen(response), 0);
        return;
    }

    int file_count = 0;
    struct dirent *entry;

    if (show_long) {
        strncat(response,
            "\n┏━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┓\n"
            "┃ Files (long view)                                                                    ┃\n"
            "┗━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┛\n"
            "┌────────────────────┬────────┬────────┬────────────────────┬────────────┬──────────────┐\n"
            "│ Name               │ Words  │ Chars  │ Last Access        │ Owner      │ Modified     │\n"
            "├────────────────────┼────────┼────────┼────────────────────┼────────────┼──────────────┤\n",
            sizeof(response) - strlen(response) - 1);
    }

    while ((entry = readdir(dir)) != NULL) {
        if (!show_all && entry->d_name[0] == '.') continue;
        if (entry->d_type != DT_REG) continue;

        char path[PATH_MAX];
        int plen = snprintf(path, sizeof(path), "%s/%s", files_dir, entry->d_name);
        if (plen < 0 || plen >= (int)sizeof(path)) {
            // Skip overly long path (avoids truncation warning treated as error)
            continue;
        }

        struct stat st;
        if (stat(path, &st) != 0) continue;

        if (show_long) {
            int word_count = 0, char_count = 0;
            count_file(path, &word_count, &char_count);

            FileMetadata meta;
            const char *owner = "unknown";
            time_t last_access_raw = st.st_atime;
            time_t last_mod_raw    = st.st_mtime;

            if (read_metadata_file(entry->d_name, &meta) == 0) {
                if (meta.owner[0]) owner = meta.owner;
                if (meta.last_accessed > 0) last_access_raw = meta.last_accessed;
                if (meta.last_modified > 0) last_mod_raw    = meta.last_modified;
            }

            char access_buf[32], mod_buf[32];
            strftime(access_buf, sizeof(access_buf), "%Y-%m-%d %H:%M", localtime(&last_access_raw));
            strftime(mod_buf,    sizeof(mod_buf),    "%Y-%m-%d %H:%M", localtime(&last_mod_raw));

            char line[256];
            snprintf(line, sizeof(line),
                "│ %-19.20s│ %6d │ %6d │ %-18.20s │ %-10.12s │ %-11.12s │\n",
                entry->d_name, word_count, char_count, access_buf, owner, mod_buf);

            strncat(response, line, sizeof(response) - strlen(response) - 1);
        } else {
            strncat(response, entry->d_name, sizeof(response) - strlen(response) - 2);
            strncat(response, "\n", sizeof(response) - strlen(response) - 1);
        }
        file_count++;
    }
    closedir(dir);

    if (show_long) {
        strncat(response,
            "└────────────────────┴────────┴────────┴────────────────────┴────────────┴──────────────┘\n",
            sizeof(response) - strlen(response) - 1);
        char summary[128];
        snprintf(summary, sizeof(summary), "Total files: %d (storage server %d)\n",
                 file_count, get_storage_id());
        strncat(response, summary, sizeof(response) - strlen(response) - 1);
    } else if (file_count == 0) {
        strncat(response, "(no files found or no access)\n", sizeof(response) - strlen(response) - 1);
    }

    send(client_sock, response, strlen(response), 0);
}