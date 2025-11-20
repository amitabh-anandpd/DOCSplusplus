#include "../../include/common.h"
#include "../../include/view.h"
#include "../../include/delete.h"
#include "../../include/write.h"
#include "../../include/info.h"
#include "../../include/stream.h"
#include "../../include/execute.h"
#include "../../include/acl.h"
#include <signal.h>
#include <sys/wait.h>
#include "../../include/undo.h" 
#include "../../include/checkpoint.h"
// Global storage server ID so helpers (e.g., write.c) can query it
static int g_storage_id = 0;
int get_storage_id(void) { return g_storage_id; }

// Reap zombie processes
void sigchld_handler(int s) {
    (void)s;
    while (waitpid(-1, NULL, WNOHANG) > 0) {}
}
// Function to read and send file content to client
void read_file(int client_sock, const char* filename, const char* username) {
    char path[512];
    char response[8192];
    FILE *fp;
    
    // Check read access
    if (!check_read_access(filename, username)) {
        sprintf(response, "Error: Access denied. You do not have read permission for '%s'\n", filename);
        send(client_sock, response, strlen(response), 0);
        return;
    }
    
    // Construct full path
    sprintf(path, "%s/storage%d/files/%s", STORAGE_DIR, get_storage_id(), filename);
    
    // Open file for reading
    fp = fopen(path, "r");
    if (!fp) {
        sprintf(response, "Error: File '%s' not found or cannot be opened\n", filename);
        send(client_sock, response, strlen(response), 0);
        return;
    }
    
    // Read entire file content
    size_t bytes_read = fread(response, 1, sizeof(response) - 1, fp);
    response[bytes_read] = '\0';  // Null-terminate
    
    fclose(fp);
    
    // Update last access time
    FileMetadata meta;
    if (read_metadata_file(filename, &meta) == 0) {
        meta.last_accessed = time(NULL);
        update_metadata_file(filename, &meta);
    }
    
    // Send content to client
    if (bytes_read == 0) {
        sprintf(response, "(File '%s' is empty)\n", filename);
    }
    
    send(client_sock, response, strlen(response), 0);
}


// Function to create an empty file
void create_file(int client_sock, const char* filename, const char* username) {
    char path[512];
    char response[256];
    FILE *fp;
    
    // Construct full path (use per-server directory)
    sprintf(path, "%s/storage%d/files/%s", STORAGE_DIR, get_storage_id(), filename);
    
    // Check if file already exists
    fp = fopen(path, "r");
    if (fp) {
        fclose(fp);
        sprintf(response, "Error: File '%s' already exists\n", filename);
        send(client_sock, response, strlen(response), 0);
        return;
    }
    
    // Create empty file
    fp = fopen(path, "w");
    if (!fp) {
        sprintf(response, "Error: Cannot create file '%s'\n", filename);
        send(client_sock, response, strlen(response), 0);
        return;
    }
    
    fclose(fp);
    
    // Create metadata file
    if (create_metadata_file(filename, username) < 0) {
        sprintf(response, "Warning: File '%s' created but metadata creation failed\n", filename);
        send(client_sock, response, strlen(response), 0);
        return;
    }
    
    sprintf(response, "Success: File '%s' created successfully\n", filename);
    send(client_sock, response, strlen(response), 0);
}

char STORAGE_BASE[256] = "storage"; 
char STORAGE_DIRE[512];

void ensure_dir(const char *path) {
    mkdir(path, 0777);
}

void initialize_storage_folders(int ss_id) {
    sprintf(STORAGE_BASE, "storage/storage%d", ss_id);

    char tmp[512];

    sprintf(tmp, "%s", STORAGE_BASE); 
    ensure_dir(tmp);

    sprintf(STORAGE_DIRE, "%s/files", STORAGE_BASE);
    ensure_dir(STORAGE_DIRE);

    sprintf(tmp, "%s/undo", STORAGE_BASE);
    ensure_dir(tmp);

    sprintf(tmp, "%s/swap", STORAGE_BASE);
    ensure_dir(tmp);

    sprintf(tmp, "%s/acl", STORAGE_BASE);
    ensure_dir(tmp);

    sprintf(tmp, "%s/meta", STORAGE_BASE);
    ensure_dir(tmp);
    sprintf(tmp, "%s/checkpoints", STORAGE_BASE);
    ensure_dir(tmp);
}

void build_file_list(char *out, size_t max_len) {
    out[0] = '\0';

    // Scan the per-server files directory
    char server_files_dir[512];
    sprintf(server_files_dir, "%s/storage%d/files", STORAGE_DIR, g_storage_id);
    
    DIR *d = opendir(server_files_dir);
    if (!d) return;

    struct dirent *entry;
    while ((entry = readdir(d)) != NULL) {
        if (entry->d_type == DT_REG) {
            strcat(out, entry->d_name);
            strcat(out, ",");
        }
    }
    closedir(d);
}

int register_with_name_server() {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) { perror("SS: socket"); exit(1); }

    struct sockaddr_in nm_addr;
    nm_addr.sin_family = AF_INET;
    nm_addr.sin_port = htons(NAME_SERVER_PORT);
    inet_pton(AF_INET, NAME_SERVER_IP, &nm_addr.sin_addr);

    if (connect(sock, (struct sockaddr*)&nm_addr, sizeof(nm_addr)) < 0) {
        perror("SS: Could not connect to Name Server");
        exit(1);
    }

    // Build initial file list (empty on first run)
    char file_list[2048];
    build_file_list(file_list, sizeof(file_list));

    char register_msg[4096];
    sprintf(register_msg,
        "TYPE:REGISTER_SS\n"
        "IP:%s\n"
        "NM_PORT:%d\n"
        "CLIENT_PORT:%d\n"
        "FILES:%s\nEND\n",
        STORAGE_SERVER_IP,
        NAME_SERVER_PORT,
        STORAGE_SERVER_PORT, 
        file_list
    );

    send(sock, register_msg, strlen(register_msg), 0);

    char response[256];
    memset(response, 0, sizeof(response));
    recv(sock, response, sizeof(response), 0);

    int ss_id = -1;
    sscanf(response, "SS_ID:%d", &ss_id);

    if (ss_id < 0) {
        printf("Name Server returned invalid ID. Exiting.\n");
        exit(1);
    }

    printf("Registered with Name Server. Assigned ID = %d\n", ss_id);

    close(sock);
    return ss_id;
}

int main() {
    printf("Starting Storage Server...\n");
    int ss_id = register_with_name_server();
    g_storage_id = ss_id; // make ID available to other translation units
    initialize_storage_folders(ss_id);
    int MY_PORT = STORAGE_SERVER_PORT + ss_id;
    printf("Storage folder created: %s\n", STORAGE_BASE);
    
    // Set up signal handler to reap zombie processes
    struct sigaction sa;
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("sigaction");
        // not fatal, continue
    }
    
    int server_fd, client_sock;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_len = sizeof(client_addr);
    char buffer[1024];

    // Create socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("Socket creation failed");
        exit(1);
    }

    // Allow immediate reuse of the port
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt failed");
        exit(1);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(MY_PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        close(server_fd);
        exit(1);
    }

    listen(server_fd, 5);
    printf("Storage server started. Listening on port %d...\n", MY_PORT);

    while (1) {
        client_sock = accept(server_fd, (struct sockaddr*)&client_addr, &addr_len);
        if (client_sock < 0) {
            perror("Accept failed");
            continue;
        }

        // Fork a child process to handle this client
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork failed");
            close(client_sock);
            continue;
        }

        if (pid > 0) {
            // Parent: close client socket and continue accepting
            close(client_sock);
            continue;
        }

        // Child process: handle the client
        close(server_fd); // Child doesn't need the listening socket

        memset(buffer, 0, sizeof(buffer));
        read(client_sock, buffer, sizeof(buffer));

        // Parse authentication credentials
        char username[64] = "", password[64] = "", command[1024] = "";
        char *line_ptr = buffer;
        char *saveptr_auth = NULL;
        char *auth_line = strtok_r(line_ptr, "\n", &saveptr_auth);
        while (auth_line) {
            if (strncmp(auth_line, "USER:", 5) == 0) {
                strncpy(username, auth_line + 5, sizeof(username) - 1);
            } else if (strncmp(auth_line, "PASS:", 5) == 0) {
                strncpy(password, auth_line + 5, sizeof(password) - 1);
            } else if (strncmp(auth_line, "CMD:", 4) == 0) {
                strncpy(command, auth_line + 4, sizeof(command) - 1);
                break;
            }
            auth_line = strtok_r(NULL, "\n", &saveptr_auth);
        }

        // Use command from here on
        strncpy(buffer, command, sizeof(buffer) - 1);
        buffer[sizeof(buffer) - 1] = '\0';

        // Remove newline / carriage return
        buffer[strcspn(buffer, "\n")] = 0;
        buffer[strcspn(buffer, "\r")] = 0;

        printf("Command received from '%s': '%s'\n", username, buffer);
        //printf("Buffer bytes: ");
        // for (int i = 0; i < strlen(buffer); i++) {
        //     printf("[%c:%d] ", buffer[i], buffer[i]);
        // }
        // printf("\n");
        // fflush(stdout);

        if (strncmp(buffer, "VIEW", 4) == 0) {
            // Parse flags from the command
            int show_all = (strstr(buffer, "-a") != NULL) || (strstr(buffer, "-la") != NULL);
            int show_long = (strstr(buffer, "-l") != NULL) || (strstr(buffer, "-al") != NULL) || (strstr(buffer, "-la") != NULL);
            
            list_files(client_sock, show_all, show_long, username);
        } 
        else if (strncmp(buffer, "READ ", 5) == 0) {
            // Extract filename from command
            char filename[256];
            sscanf(buffer + 5, "%s", filename);  // Skip "READ " and get filename
            
            if (strlen(filename) == 0) {
                char msg[] = "Error: Please specify a filename\n";
                send(client_sock, msg, strlen(msg), 0);
            } else {
                read_file(client_sock, filename, username);
            }
        } 
        else if (strncmp(buffer, "CREATE ", 7) == 0) {
            // Extract filename from command
            char filename[256];
            sscanf(buffer + 7, "%s", filename);  // Skip "CREATE " and get filename
            
            if (strlen(filename) == 0) {
                char msg[] = "Error: Please specify a filename\n";
                send(client_sock, msg, strlen(msg), 0);
            } else {
                create_file(client_sock, filename, username);
            }
        }
        else if (strncmp(buffer, "DELETE ", 7) == 0) {
            // Extract filename from command
            char filename[256];
            sscanf(buffer + 7, "%s", filename);
            
            if (strlen(filename) == 0) {
                char msg[] = "Error: Please specify a filename\n";
                send(client_sock, msg, strlen(msg), 0);
            } else {
                delete_from_storage(client_sock, filename, username);
            }
        }
        else if (strncmp(buffer, "WRITE ", 6) == 0) {
            char filename[256];
            int sentence_num;
            if (sscanf(buffer + 6, "%s %d", filename, &sentence_num) == 2) {
                write_to_file(client_sock, filename, sentence_num, username);
                // write_to_file handles the interactive loop internally
                // and will complete when user sends ETIRW
            } else {
                char msg[] = "Usage: WRITE <filename> <sentence_number>\n";
                send(client_sock, msg, strlen(msg), 0);
            }
            // Don't close or continue here - fall through to normal cleanup
        }
        else if (strncmp(buffer, "INFO ", 5) == 0) {
            char filename[256];
            sscanf(buffer + 5, "%s", filename);

            if (strlen(filename) == 0) {
                char msg[] = "Error: Please specify a filename\n";
                send(client_sock, msg, strlen(msg), 0);
            } else {
                file_info(client_sock, filename, username);
            }
        }
        else if (strncmp(buffer, "STREAM ", 7) == 0) {
            char filename[256];
            sscanf(buffer + 7, "%s", filename);
            
            if (strlen(filename) == 0) {
                char msg[] = "Error: Please specify a filename\n";
                send(client_sock, msg, strlen(msg), 0);
            } else {
                stream_file(client_sock, filename, username);
            }
        }
        // else if (strncmp(buffer, "EXEC ", 5) == 0) {
        //     char filename[256];
        //     sscanf(buffer + 5, "%s", filename); // extract filename

        //     if (strlen(filename) == 0) {
        //         char msg[] = "Error: Please specify a filename\n";
        //         send(client_sock, msg, strlen(msg), 0);
        //     } else {
        //         execute_file(client_sock, filename, username);
        //     }
        // }
        else if (strncmp(buffer, "UNDO ", 5) == 0) {
            char filename[256];
            sscanf(buffer + 5, "%s", filename);
            
            if (strlen(filename) == 0) {
                char msg[] = "Error: Please specify a filename\n";
                send(client_sock, msg, strlen(msg), 0);
            }
            else {
                undo_last_change(client_sock, filename, username);
            }
        }

        // Add after the UNDO command handler (around line 200+)

        else if (strncmp(buffer, "CHECKPOINT ", 11) == 0) {
            char filename[256], tag[64];
            if (sscanf(buffer + 11, "%s %s", filename, tag) == 2) {
                checkpoint_create(client_sock, filename, tag, username, g_storage_id);
            } else {
                char msg[] = "Usage: CHECKPOINT <filename> <tag>\n";
                send(client_sock, msg, strlen(msg), 0);
            }
        }
        else if (strncmp(buffer, "VIEWCHECKPOINT ", 15) == 0) {
            char filename[256], tag[64];
            if (sscanf(buffer + 15, "%s %s", filename, tag) == 2) {
                checkpoint_view(client_sock, filename, tag, username, g_storage_id);
            } else {
                char msg[] = "Usage: VIEWCHECKPOINT <filename> <tag>\n";
                send(client_sock, msg, strlen(msg), 0);
            }
        }
        else if (strncmp(buffer, "REVERT ", 7) == 0) {
            char filename[256], tag[64];
            if (sscanf(buffer + 7, "%s %s", filename, tag) == 2) {
                checkpoint_revert(client_sock, filename, tag, username, g_storage_id);
            } else {
                char msg[] = "Usage: REVERT <filename> <tag>\n";
                send(client_sock, msg, strlen(msg), 0);
            }
        }
        else if (strncmp(buffer, "LISTCHECKPOINTS ", 16) == 0) {
            char filename[256];
            if (sscanf(buffer + 16, "%s", filename) == 1) {
                checkpoint_list(client_sock, filename, username, g_storage_id);
            } else {
                char msg[] = "Usage: LISTCHECKPOINTS <filename>\n";
                send(client_sock, msg, strlen(msg), 0);
            }
        }
        else if (strncmp(buffer, "ADDACCESS ", 10) == 0) {
            // Parse: ADDACCESS -R|-W <filename> <target_username>
            char flag[8], filename[256], target_user[64];
            char response[512];
            
            if (sscanf(buffer + 10, "%s %s %s", flag, filename, target_user) != 3) {
                char msg[] = "Usage: ADDACCESS -R|-W <filename> <target_username>\n";
                send(client_sock, msg, strlen(msg), 0);
            } else {
                // Check if file exists and requester is the owner
                FileMetadata meta;
                if (read_metadata_file(filename, &meta) != 0) {
                    snprintf(response, sizeof(response), "Error: File '%s' not found\n", filename);
                    send(client_sock, response, strlen(response), 0);
                } else if (strcmp(meta.owner, username) != 0) {
                    snprintf(response, sizeof(response), "Error: Only the owner can grant access to '%s'\n", filename);
                    send(client_sock, response, strlen(response), 0);
                } else {
                    // Add access
                    int result = -1;
                    if (strcmp(flag, "-R") == 0) {
                        result = add_read_access(filename, target_user);
                        if (result == 0) {
                            snprintf(response, sizeof(response), "Success: Read access granted to '%s' for file '%s'\n", target_user, filename);
                        } else {
                            snprintf(response, sizeof(response), "Info: User '%s' already has read access to '%s'\n", target_user, filename);
                        }
                    } else if (strcmp(flag, "-W") == 0) {
                        result = add_write_access(filename, target_user);
                        if (result == 0) {
                            snprintf(response, sizeof(response), "Success: Write access granted to '%s' for file '%s'\n", target_user, filename);
                        } else {
                            snprintf(response, sizeof(response), "Info: User '%s' already has write access to '%s'\n", target_user, filename);
                        }
                    } else {
                        snprintf(response, sizeof(response), "Error: Invalid flag '%s'. Use -R for read or -W for write\n", flag);
                    }
                    send(client_sock, response, strlen(response), 0);
                }
            }
        }
        else if (strncmp(buffer, "REMACCESS ", 10) == 0) {
            // Parse: REMACCESS <filename> <target_username>
            char filename[256], target_user[64];
            char response[512];
            
            if (sscanf(buffer + 10, "%s %s", filename, target_user) != 2) {
                char msg[] = "Usage: REMACCESS <filename> <target_username>\n";
                send(client_sock, msg, strlen(msg), 0);
            } else {
                // Check if file exists and requester is the owner
                FileMetadata meta;
                if (read_metadata_file(filename, &meta) != 0) {
                    snprintf(response, sizeof(response), "Error: File '%s' not found\n", filename);
                    send(client_sock, response, strlen(response), 0);
                } else if (strcmp(meta.owner, username) != 0) {
                    snprintf(response, sizeof(response), "Error: Only the owner can revoke access to '%s'\n", filename);
                    send(client_sock, response, strlen(response), 0);
                } else if (strcmp(target_user, username) == 0) {
                    snprintf(response, sizeof(response), "Error: Cannot revoke owner's access\n");
                    send(client_sock, response, strlen(response), 0);
                } else {
                    // Remove access
                    int result = remove_all_access(filename, target_user);
                    if (result == 0) {
                        snprintf(response, sizeof(response), "Success: All access revoked for '%s' on file '%s'\n", target_user, filename);
                    } else {
                        snprintf(response, sizeof(response), "Error: Failed to revoke access\n");
                    }
                    send(client_sock, response, strlen(response), 0);
                }
            }
        }
        else {
            char msg[] = "Invalid command.\n";
            send(client_sock, msg, strlen(msg), 0);
        }

        close(client_sock);
        exit(0); // Child process exits after handling the client
    }

    close(server_fd);
    return 0;
}