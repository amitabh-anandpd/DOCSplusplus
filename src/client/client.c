#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define PORT 8080

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
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection failed");
        close(sock);
        exit(1);
    }

    printf("Enter command - \n1. VIEW\n2. VIEW -a\n3. VIEW -l\n4. VIEW -al\n5. READ <filename>\n6. CREATE <filename>\n7. DELETE <filename>\n8. WRITE <filename> <sentence number>\n9. INFO <filename>\nEnter command: ");
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
else {
    // For all other commands
    memset(buffer, 0, sizeof(buffer));
    read(sock, buffer, sizeof(buffer));
    printf("\n--- Server Response ---\n%s\n", buffer);
}


    close(sock);
    return 0;
}
