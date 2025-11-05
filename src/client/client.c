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

    printf("Enter command - \n1. VIEW\n2. VIEW -a\n3. VIEW -l\n4. VIEW -al\n5. READ <filename>\n6. CREATE <filename>\n7. DELETE <filename>\n8. WRITE <filename> <sentence number>\nEnter command: ");
    fgets(command, sizeof(command), stdin);
    command[strcspn(command, "\n")] = 0; // remove newline

    send(sock, command, strlen(command), 0);

    memset(buffer, 0, sizeof(buffer));
    read(sock, buffer, sizeof(buffer));
    printf("\n--- Server Response ---\n%s\n", buffer);

    close(sock);
    return 0;
}
