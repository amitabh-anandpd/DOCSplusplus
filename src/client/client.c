#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <strings.h>
#include "../../include/common.h"
#include "../../include/client_write.h"

int main() {
    int sock;
    struct sockaddr_in server_addr;
    char buffer[2048], command[100];
    int target_port;
    char username[64], password[64];
    
    // Print welcome banner
    printf("\n");
    printf("╔══════════════════════════════════════════════════════════════════════════╗\n");
    printf("║                                                                          ║\n");
    printf("║   ██████╗  ██████╗  ██████╗███████╗   ██╗      ██╗                       ║\n");
    printf("║   ██╔══██╗██╔═══██╗██╔════╝██╔════╝   ██╚═╗    ██╚═╗                     ║\n");
    printf("║   ██║  ██║██║   ██║██║     ███████╗████████ ████████                     ║\n");
    printf("║   ██║  ██║██║   ██║██║     ╚════██║   ██║      ██║                       ║\n");
    printf("║   ██████╔╝╚██████╔╝╚██████╗███████║   ██╝      ██╝                       ║\n");
    printf("║   ╚═════╝  ╚═════╝  ╚═════╝╚══════╝╚══════╝╚══════╝                      ║\n");
    printf("║                                                                          ║\n");
    printf("║          Distributed Network File System with Advanced Features          ║\n");
    printf("║                                                                          ║\n");
    printf("╚══════════════════════════════════════════════════════════════════════════╝\n");
    printf("\n");
    
    // Authentication
    printf("═══════════════════════ User Authentication ═══════════════════════\n");
    
    int authenticated = 0;
    while (!authenticated) {
        printf("Username: ");
        fflush(stdout);
        if (!fgets(username, sizeof(username), stdin)) {
            printf("Failed to read username.\n");
            return 1;
        }
        username[strcspn(username, "\n")] = 0; // Remove newline
        
        printf("Password: ");
        fflush(stdout);
        if (!fgets(password, sizeof(password), stdin)) {
            printf("Failed to read password.\n");
            return 1;
        }
        password[strcspn(password, "\n")] = 0; // Remove newline
        
        // Connect to name server to authenticate
        sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) {
            perror("Socket creation failed");
            return 1;
        }
        
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(NAME_SERVER_PORT);
        server_addr.sin_addr.s_addr = inet_addr(NAME_SERVER_IP);
        
        if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
            perror("Connection to Name Server failed");
            close(sock);
            return 1;
        }
        
        // Send authentication request
        char auth_msg[512];
        snprintf(auth_msg, sizeof(auth_msg), "TYPE:AUTH\nUSER:%s\nPASS:%s\n", username, password);
        send(sock, auth_msg, strlen(auth_msg), 0);
        
        // Receive authentication response
        memset(buffer, 0, sizeof(buffer));
        ssize_t bytes = recv(sock, buffer, sizeof(buffer) - 1, 0);
        if (bytes > 0) {
            buffer[bytes] = '\0';
            if (strstr(buffer, "AUTH:SUCCESS") != NULL) {
                authenticated = 1;
                printf("\n✓ Authentication successful! Welcome, %s!\n\n", username);
            } else {
                printf("\n✗ Authentication failed. Invalid username or password.\n");
                printf("Please try again.\n\n");
            }
        } else {
            printf("\n✗ Authentication failed. No response from server.\n");
            printf("Please try again.\n\n");
        }
        
        close(sock);
    }
    
    // Print quick help once
    printf("Available Commands:\n");
    printf("  ┌─────────────────────────────────────────────────────────────────┐\n");
    printf("  │ VIEW | VIEW -a | VIEW -l | VIEW -al                             │\n");
    printf("  │ READ <filename>     │  CREATE <filename>  │  DELETE <filename>  │\n");
    printf("  │ WRITE <filename> <sentence number>  │  INFO <filename>          │\n");
    printf("  │ STREAM <filename>   │  EXEC <filename>                          │\n");
    printf("  │ ADDACCESS -R|-W <filename> <username>  │  REMACCESS <filename> <username> │\n");
    printf("  │ EXIT or QUIT - Leave the client                                 │\n");
    printf("  │ LIST                                │\n");
    printf("  │ UNDO <filename>                                │\n");
    printf("  └─────────────────────────────────────────────────────────────────┘\n");
    printf("\n");

    while (1) {
        printf("Client: ");
        fflush(stdout);

        if (!fgets(command, sizeof(command), stdin)) {
            // EOF (Ctrl+D)
            printf("\n");
            break;
        }

        // remove newline
        command[strcspn(command, "\n")] = 0;

        // skip empty
        if (strlen(command) == 0) continue;

        // exit commands
        if (strcasecmp(command, "EXIT") == 0 || strcasecmp(command, "QUIT") == 0) break;

        // Decide where to connect: STREAM should go straight to storage server
        // Special handling for STREAM command
if (strncmp(command, "STREAM", 6) == 0) {
    // Extract filename
    char filename[256];
    sscanf(command + 7, "%s", filename);
    
    // Step 1: Connect to Name Server to get storage server ID
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("Socket creation failed");
        continue;
    }
    
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(NAME_SERVER_PORT);
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    
    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection to Name Server failed");
        close(sock);
        continue;
    }
    
    // Send INFO command to get storage server ID
    char info_cmd[512];
    snprintf(info_cmd, sizeof(info_cmd), "INFO %s", filename);
    send(sock, info_cmd, strlen(info_cmd), 0);
    
    // Read response
    ssize_t bytes = read(sock, buffer, sizeof(buffer) - 1);
    buffer[bytes] = '\0';
    close(sock);
    
    // Parse storage server ID
    int ss_id = -1;
    char *id_line = strstr(buffer, "Storage Server ID:");
    if (id_line) {
        sscanf(id_line, "Storage Server ID: %d", &ss_id);
    }
    
    if (ss_id < 0) {
        printf("Error: Could not find storage server for file '%s'\n", filename);
        printf("%s\n", buffer);
        continue;
    }
    
    // Step 2: Connect to the correct storage server
    target_port = STORAGE_SERVER_PORT + ss_id;
} else {
    target_port = NAME_SERVER_PORT;
}

        sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) {
            perror("Socket creation failed");
            continue;
        }

        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(target_port);
        server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

        if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
            perror("Connection failed");
            close(sock);
            continue;
        }

        // Prepend credentials to command
        char authenticated_cmd[2048];
        snprintf(authenticated_cmd, sizeof(authenticated_cmd), "USER:%s\nPASS:%s\nCMD:%s", 
                 username, password, command);
        
        send(sock, authenticated_cmd, strlen(authenticated_cmd), 0);

        // If STREAM we read directly from storage server
        if (strncmp(command, "STREAM", 6) == 0) {
            printf("\n--- Streaming Content ---\n");

            ssize_t bytes;
            while ((bytes = read(sock, buffer, sizeof(buffer) - 1)) > 0) {
                buffer[bytes] = '\0';
                printf("%s", buffer);
                fflush(stdout);
            }

            if (bytes == 0)
                printf("\n\n[INFO] Stream ended successfully.\n");
            else
                printf("\n\n[ERROR] Connection lost while streaming (server may have gone down).\n");
        }
        else if(strncmp(command, "WRITE", 5) == 0) {
            char filename[256];
            int sentence_num;
            sscanf(command + 6, "%s %d", filename, &sentence_num);
            if (strlen(filename) == 0) {
                printf("Error: Please specify a filename\n");
            } else {
                client_write(sock, filename, sentence_num);
            }
        }
        else {
            // For all other commands: read until server closes the connection
            printf("\n--- Server Response ---\n");
            ssize_t bytes;
            while ((bytes = read(sock, buffer, sizeof(buffer) - 1)) > 0) {
                buffer[bytes] = '\0';
                printf("%s", buffer);
            }

            if (bytes == 0) {
                printf("\n[INFO] Response complete.\n");
            } else {
                printf("\n[ERROR] Connection closed unexpectedly while reading response.\n");
            }
        }

        close(sock);
    }

    return 0;
}
