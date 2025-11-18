#include "../../include/common.h"
#include "../../include/write.h"

void client_write(int sock, const char *filename, int sentence_num) {
    char cmd[256];
    sprintf(cmd, "WRITE %s %d", filename, sentence_num);
    send(sock, cmd, strlen(cmd), 0);

    char buf[1024];
    memset(buf, 0, sizeof(buf));
    ssize_t r = recv(sock, buf, sizeof(buf) - 1, 0);
    if (r > 0) {
        buf[r] = '\0';
        printf("%s", buf); // prints the "locked + instructions"
    }

    while (1) {
        char input[512];
        printf("> "); // prompt for user
        fflush(stdout);
        
        if (!fgets(input, sizeof(input), stdin)) break;
        
        // Send input to server
        send(sock, input, strlen(input), 0);

        // Receive response
        memset(buf, 0, sizeof(buf));
        r = recv(sock, buf, sizeof(buf) - 1, 0);
        if (r <= 0) {
            printf("Connection closed by server.\n");
            break;
        }
        buf[r] = '\0';
        printf("%s", buf);

        // Check if user typed ETIRW to finish
        if (strncmp(input, "ETIRW", 5) == 0)
            break;
    }
}
