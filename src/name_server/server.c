
#include "../../include/common.h"

#include <netinet/in.h>
#include <errno.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/select.h>

// Storage server registry (kept in parent process)
#define MAX_SS 32
typedef struct {
    int id;
    char ip[64];
    int nm_port;
    char files[4096];
    time_t last_seen;
    int active;
} StorageServerInfo;

static StorageServerInfo storage_servers[MAX_SS];
static int num_storage_servers = 0;

// Add a storage server entry and return its id, or -1 on failure
static int add_storage_server(const char *ip, int nm_port, const char *files) {
    if (num_storage_servers >= MAX_SS) return -1;
    int idx = num_storage_servers;
    int id = idx + 1; // ss_id starts from 1
    storage_servers[idx].id = id;
    strncpy(storage_servers[idx].ip, ip ? ip : "127.0.0.1", sizeof(storage_servers[idx].ip)-1);
    storage_servers[idx].nm_port = nm_port;
    storage_servers[idx].last_seen = time(NULL);
    storage_servers[idx].active = 1;
    if (files) {
        strncpy(storage_servers[idx].files, files, sizeof(storage_servers[idx].files)-1);
    } else {
        storage_servers[idx].files[0] = '\0';
    }
    num_storage_servers++;
    return id;
}

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

        // Peek at the incoming data to detect registration messages
        char peek[8192];
        ssize_t peek_n = recv(client_sock, peek, sizeof(peek)-1, MSG_PEEK);
        if (peek_n < 0) peek_n = 0;
        peek[peek_n] = '\0';

        // If this is a registration from a storage server, read and store it in parent
        if (peek_n > 6 && strstr(peek, "TYPE:REGISTER_SS") == peek) {
            // read the full registration message (consume it)
            char regbuf[8192];
            ssize_t rn = recv(client_sock, regbuf, sizeof(regbuf)-1, 0);
            if (rn < 0) rn = 0;
            regbuf[rn] = '\0';

            // parse registration lines
            char *saveptr = NULL;
            char *line = strtok_r(regbuf, "\n", &saveptr);
            char ipstr[64] = "127.0.0.1";
            int nm_port = NAME_SERVER_PORT;
            char files[4096] = "";
            while (line) {
                if (strncmp(line, "IP:", 3) == 0) {
                    strncpy(ipstr, line + 3, sizeof(ipstr)-1);
                } else if (strncmp(line, "NM_PORT:", 8) == 0) {
                    nm_port = atoi(line + 8);
                } else if (strncmp(line, "FILES:", 6) == 0) {
                    strncpy(files, line + 6, sizeof(files)-1);
                }
                line = strtok_r(NULL, "\n", &saveptr);
            }

            int ss_id = add_storage_server(ipstr, nm_port, files);
            char resp[128];
            if (ss_id >= 0) {
                snprintf(resp, sizeof(resp), "SS_ID:%d\n", ss_id);
            } else {
                snprintf(resp, sizeof(resp), "SS_ID:-1\n");
            }
            send(client_sock, resp, strlen(resp), 0);
            close(client_sock);
            continue;
        }

        // Not a registration: fork to handle the connection as before
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

        // Read any initial bytes client sent (command)
        char buf[4096];
        ssize_t n = recv(client_sock, buf, sizeof(buf) - 1, 0);
        if (n < 0) n = 0;
        buf[n] = '\0';

        // If this is an EXEC command, handle it on the name server:
        // 1) ask storage server for the file content (send READ <filename>)
        // 2) execute each non-empty line locally via popen()
        // 3) stream outputs back to the client
        if (n > 5 && strncmp(buf, "EXEC ", 5) == 0) {
            // extract filename
            char filename[256];
            if (sscanf(buf + 5, "%255s", filename) != 1) {
                char msg[] = "Error: EXEC requires a filename\n";
                send(client_sock, msg, strlen(msg), 0);
                close(storage_sock);
                close(client_sock);
                exit(0);
            }

            // ask storage server for file content
            char read_cmd[512];
            snprintf(read_cmd, sizeof(read_cmd), "READ %s", filename);
            ssize_t sent = 0;
            ssize_t tosend = strlen(read_cmd);
            while (sent < tosend) {
                ssize_t s = send(storage_sock, read_cmd + sent, tosend - sent, 0);
                if (s <= 0) break;
                sent += s;
            }

            // read full file content from storage server
            char *file_buf = NULL;
            size_t file_cap = 0, file_len = 0;
            char tmp[4096];
            ssize_t r;
            while ((r = recv(storage_sock, tmp, sizeof(tmp), 0)) > 0) {
                if (file_len + r + 1 > file_cap) {
                    size_t rr = (size_t)r;
                    size_t newcap = (file_cap == 0) ? (rr + 1) : (file_cap * 2 + rr + 1);
                    char *nb = realloc(file_buf, newcap);
                    if (!nb) break; // allocation failure
                    file_buf = nb;
                    file_cap = newcap;
                }
                memcpy(file_buf + file_len, tmp, r);
                file_len += r;
            }
            if (file_buf) file_buf[file_len] = '\0';

            // close storage connection early
            close(storage_sock);

            if (!file_buf || file_len == 0) {
                const char *fmt = "Error: Could not read file '%s' or file is empty\n";
                size_t mlen = snprintf(NULL, 0, fmt, filename) + 1;
                char *msg = malloc(mlen);
                if (msg) {
                    snprintf(msg, mlen, fmt, filename);
                    send(client_sock, msg, strlen(msg), 0);
                    free(msg);
                } else {
                    const char *fallback = "Error: Could not read file or file is empty\n";
                    send(client_sock, fallback, strlen(fallback), 0);
                }
                free(file_buf);
                close(client_sock);
                exit(0);
            }

            // Execute each line locally and stream output to client
            // We'll tokenize by newline
            char *saveptr = NULL;
            char *line = strtok_r(file_buf, "\n", &saveptr);
            while (line) {
                // trim leading whitespace
                while (*line == ' ' || *line == '\t') line++;
                // skip empty lines and fences
                if (line[0] != '\0' && strncmp(line, "```", 3) != 0) {
                    // run command
                    FILE *cmd_fp = popen(line, "r");
                    if (!cmd_fp) {
                        char err[512];
                        snprintf(err, sizeof(err), "ERROR: Failed to execute command: %s\n", line);
                        send(client_sock, err, strlen(err), 0);
                    } else {
                        char outbuf[1024];
                        while (fgets(outbuf, sizeof(outbuf), cmd_fp)) {
                            send(client_sock, outbuf, strlen(outbuf), 0);
                        }
                        pclose(cmd_fp);
                    }
                }
                line = strtok_r(NULL, "\n", &saveptr);
            }

            free(file_buf);
            close(client_sock);
            exit(0);
        }

        // Not an EXEC command: forward initial bytes to storage server (if any)
        if (n > 0) {
            ssize_t sent2 = 0;
            while (sent2 < n) {
                ssize_t s = send(storage_sock, buf + sent2, n - sent2, 0);
                if (s <= 0) break;
                sent2 += s;
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