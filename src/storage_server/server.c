#include "../../include/common.h"
#include "../../include/view.h"
#include "../../include/delete.h"
#include "../../include/write.h"
#include "../../include/info.h"
#include "../../include/stream.h"
#include "../../include/execute.h"
// Function to read and send file content to client
void read_file(int client_sock, const char* filename) {
    char path[512];
    char response[8192];
    FILE *fp;
    // Construct full path
    sprintf(path, "%s/%s", STORAGE_DIR, filename);
    
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
    
    // Send content to client
    if (bytes_read == 0) {
        sprintf(response, "(File '%s' is empty)\n", filename);
    }
    
    send(client_sock, response, strlen(response), 0);
}


// Function to create an empty file
void create_file(int client_sock, const char* filename) {
    char path[512];
    char response[256];
    FILE *fp;
    
    // Construct full path
    sprintf(path, "%s/%s", STORAGE_DIR, filename);
    
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
    
    sprintf(response, "Success: File '%s' created successfully\n", filename);
    send(client_sock, response, strlen(response), 0);
}

char STORAGE_BASE[256] = "storage"; 
char STORAGE_DIRE[512];

void ensure_dir(const char *path) {
    mkdir(path, 0777);
}

void initialize_storage_folders(int ss_id) {
    sprintf(STORAGE_BASE, "storage%d", ss_id);

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
}

void build_file_list(char *out, size_t max_len) {
    out[0] = '\0';

    DIR *d = opendir(STORAGE_DIR);
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
    initialize_storage_folders(ss_id);
    int MY_PORT = STORAGE_SERVER_PORT + ss_id;
    printf("Storage folder created: %s\n", STORAGE_BASE);
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

        memset(buffer, 0, sizeof(buffer));
        read(client_sock, buffer, sizeof(buffer));

        // Remove newline / carriage return
        buffer[strcspn(buffer, "\n")] = 0;
        buffer[strcspn(buffer, "\r")] = 0;

        printf("Command received: '%s'\n", buffer);
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
            
            list_files(client_sock, show_all, show_long);
        } 
        else if (strncmp(buffer, "READ ", 5) == 0) {
            // Extract filename from command
            char filename[256];
            sscanf(buffer + 5, "%s", filename);  // Skip "READ " and get filename
            
            if (strlen(filename) == 0) {
                char msg[] = "Error: Please specify a filename\n";
                send(client_sock, msg, strlen(msg), 0);
            } else {
                read_file(client_sock, filename);
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
                create_file(client_sock, filename);
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
                delete_from_storage(client_sock, filename);
            }
        }
        else if (strncmp(buffer, "WRITE ", 6) == 0) {
            char filename[256];
            int sentence_num;
            if (sscanf(buffer + 6, "%s %d", filename, &sentence_num) == 2) {
                write_to_file(client_sock, filename, sentence_num);
            } else {
                char msg[] = "Usage: WRITE <filename> <sentence_number>\n";
                send(client_sock, msg, strlen(msg), 0);
            }
            close(client_sock);
            continue;
        }
        else if (strncmp(buffer, "INFO ", 5) == 0) {
            char filename[256];
            sscanf(buffer + 5, "%s", filename);

            if (strlen(filename) == 0) {
                char msg[] = "Error: Please specify a filename\n";
                send(client_sock, msg, strlen(msg), 0);
            } else {
                file_info(client_sock, filename);
            }
        }
        else if (strncmp(buffer, "STREAM ", 7) == 0) {
            char filename[256];
            sscanf(buffer + 7, "%s", filename);
            
            if (strlen(filename) == 0) {
                char msg[] = "Error: Please specify a filename\n";
                send(client_sock, msg, strlen(msg), 0);
            } else {
                stream_file(client_sock, filename);
            }
        }
        else if (strncmp(buffer, "EXEC ", 5) == 0) {
            char filename[256];
            sscanf(buffer + 5, "%s", filename); // extract filename

            if (strlen(filename) == 0) {
                char msg[] = "Error: Please specify a filename\n";
                send(client_sock, msg, strlen(msg), 0);
            } else {
                execute_file(client_sock, filename);
            }
        }

        else {
            char msg[] = "Invalid command.\n";
            send(client_sock, msg, strlen(msg), 0);
        }

        close(client_sock);
    }

    close(server_fd);
    return 0;
}