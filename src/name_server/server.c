#include "../../include/common.h"

#include <netinet/in.h>
#include <errno.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/select.h>

// Reap dead child processes
void sigchld_handler(int s) {
    (void)s;
    while (waitpid(-1, NULL, WNOHANG) > 0) {}
}

// Proxy data between two sockets bidirectionally until both sides close
void proxy_bidirectional(int a_sock, int b_sock) {
    fd_set read_fds;
    int maxfd = (a_sock > b_sock) ? a_sock : b_sock;
    char buf[4096];
    int a_open = 1, b_open = 1;

    while (a_open || b_open) {
        FD_ZERO(&read_fds);
        if (a_open) FD_SET(a_sock, &read_fds);
        if (b_open) FD_SET(b_sock, &read_fds);

        int sel = select(maxfd + 1, &read_fds, NULL, NULL, NULL);
        if (sel < 0) {
            if (errno == EINTR) continue;
            break;
        }

        if (a_open && FD_ISSET(a_sock, &read_fds)) {
            ssize_t n = recv(a_sock, buf, sizeof(buf), 0);
            if (n <= 0) {
                // peer closed or error
                shutdown(b_sock, SHUT_WR);
                a_open = 0;
            } else {
                ssize_t sent = 0;
                while (sent < n) {
                    ssize_t s = send(b_sock, buf + sent, n - sent, 0);
                    if (s <= 0) { b_open = 0; break; }
                    sent += s;
                }
            }
        }

        if (b_open && FD_ISSET(b_sock, &read_fds)) {
            ssize_t n = recv(b_sock, buf, sizeof(buf), 0);
            if (n <= 0) {
                shutdown(a_sock, SHUT_WR);
                b_open = 0;
            } else {
                ssize_t sent = 0;
                while (sent < n) {
                    ssize_t s = send(a_sock, buf + sent, n - sent, 0);
                    if (s <= 0) { a_open = 0; break; }
                    sent += s;
                }
            }
        }
    }
}

int main() {
    int listen_fd, client_sock;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_len = sizeof(client_addr);

    // Handle SIGCHLD to avoid zombies
    struct sigaction sa;
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("sigaction");
        // not fatal
    }

    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("Name server socket creation failed");
        exit(1);
    }

    int opt = 1;
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt failed");
        close(listen_fd);
        exit(1);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(NAME_SERVER_PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(listen_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Name server bind failed");
        close(listen_fd);
        exit(1);
    }

    if (listen(listen_fd, 10) < 0) {
        perror("listen failed");
        close(listen_fd);
        exit(1);
    }

    printf("Name server started. Listening on port %d...\n", NAME_SERVER_PORT);

    while (1) {
        client_sock = accept(listen_fd, (struct sockaddr*)&client_addr, &addr_len);
        if (client_sock < 0) {
            perror("Accept failed");
            continue;
        }

        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            close(client_sock);
            continue;
        }

        if (pid > 0) {
            // parent
            close(client_sock);
            continue;
        }

        // child: handle proxying between client_sock and storage server
        close(listen_fd);

        int storage_sock = socket(AF_INET, SOCK_STREAM, 0);
        if (storage_sock < 0) {
            perror("socket to storage failed");
            close(client_sock);
            exit(1);
        }

        struct sockaddr_in storage_addr;
        storage_addr.sin_family = AF_INET;
        storage_addr.sin_port = htons(STORAGE_SERVER_PORT);
        storage_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

        if (connect(storage_sock, (struct sockaddr*)&storage_addr, sizeof(storage_addr)) < 0) {
            char msg[256];
            snprintf(msg, sizeof(msg), "Error: Could not connect to storage server on port %d\n", STORAGE_SERVER_PORT);
            send(client_sock, msg, strlen(msg), 0);
            close(storage_sock);
            close(client_sock);
            exit(1);
        }

        // Read any initial bytes client sent (command) and forward them
        char buf[4096];
        ssize_t n = recv(client_sock, buf, sizeof(buf), 0);
        if (n > 0) {
            ssize_t sent = 0;
            while (sent < n) {
                ssize_t s = send(storage_sock, buf + sent, n - sent, 0);
                if (s <= 0) break;
                sent += s;
            }
        }

        // Now proxy the rest bidirectionally
        proxy_bidirectional(client_sock, storage_sock);

        close(storage_sock);
        close(client_sock);
        exit(0);
    }

    close(listen_fd);
    return 0;
}
