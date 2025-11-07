#include "../../include/common.h"
#include "../../include/client_write.h"

int main() {
    int sock;
    struct sockaddr_in server_addr;
    char buffer[2048], command[100];

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("Socket creation failed");
        exit(1);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(NAME_SERVER_PORT);
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection failed");
        close(sock);
        exit(1);
    }

    printf("Enter command - \n"
        "1. VIEW\n"
        "2. VIEW -a\n"
        "3. VIEW -l\n"
        "4. VIEW -al\n"
        "5. READ <filename>\n"
        "6. CREATE <filename>\n"
        "7. DELETE <filename>\n"
        "8. WRITE <filename> <sentence number>\n"
        "9. INFO <filename>\n"
        "10. STREAM <filename>\n"
        "Enter command: ");
    fgets(command, sizeof(command), stdin);
    command[strcspn(command, "\n")] = 0; // remove newline

    send(sock, command, strlen(command), 0);

    // Check if this is a STREAM command
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
        // For all other commands
        memset(buffer, 0, sizeof(buffer));
        read(sock, buffer, sizeof(buffer));
        printf("\n--- Server Response ---\n%s\n", buffer);
    }


    close(sock);
    return 0;
}
