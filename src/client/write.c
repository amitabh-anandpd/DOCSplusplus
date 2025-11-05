#include "../../include/common.h"
#include "../../include/write.h"

void client_write(int sock, const char *filename, int sentence_num) {
    char cmd[256];
    sprintf(cmd, "WRITE %s %d", filename, sentence_num);
    send(sock, cmd, strlen(cmd), 0);

    char buf[1024];
    recv(sock, buf, sizeof(buf), 0);
    printf("%s", buf); // prints the "locked + instructions"

    while (1) {
        char input[512];
        fgets(input, sizeof(input), stdin);
        send(sock, input, strlen(input), 0);

        recv(sock, buf, sizeof(buf), 0);
        printf("%s", buf);

        if (strncmp(input, "ETIRW", 5) == 0)
            break;
    }
}
