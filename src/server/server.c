#include "../../include/common.h"
#include "../../include/view.h"


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

int main() {
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
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        close(server_fd);
        exit(1);
    }

    listen(server_fd, 5);
    printf("Server started. Listening on port %d...\n", PORT);

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
    sscanf(buffer + 7, "%s", filename);  // Skip "READ " and get filename
    
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
else {
    char msg[] = "Invalid command.\n";
    send(client_sock, msg, strlen(msg), 0);
}

        close(client_sock);
    }

    close(server_fd);
    return 0;
}