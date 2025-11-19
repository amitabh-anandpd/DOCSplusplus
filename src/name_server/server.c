#include "../../include/common.h"
#include "../../include/logger.h"

#include "../../include/list.h"
#include "../../include/file_index.h"

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
    int nm_port;            // name server port (informational)
    int client_port;        // storage server's client-facing port
    char files[4096];
    time_t last_seen;
    int active;
} StorageServerInfo;

static StorageServerInfo storage_servers[MAX_SS];
static int num_storage_servers = 0;


static FileIndex file_index;

// Update file index from a storage server by sending VIEW and parsing the result
void update_file_index_from_ss(const char *ip, int client_port, int ss_id) {
    int ss_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (ss_sock < 0) return;
    struct sockaddr_in sa;
    sa.sin_family = AF_INET;
    sa.sin_port = htons(client_port);
    sa.sin_addr.s_addr = inet_addr(ip);
    struct timeval tv; tv.tv_sec = 1; tv.tv_usec = 0;
    setsockopt(ss_sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);
    setsockopt(ss_sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof tv);
    int conn_res = connect(ss_sock, (struct sockaddr*)&sa, sizeof(sa));
    if (conn_res != 0) {
        log_event(LOG_ERROR, "[DEBUG] connect() to SS %d at %s:%d failed: %s", ss_id, ip, client_port, strerror(errno));
    }
    if (conn_res == 0) {
        // Send VIEW command with authentication (use root/root or valid admin user)
        const char *view_cmd = "USER:root\nPASS:root\nCMD:VIEW\n";
        send(ss_sock, view_cmd, strlen(view_cmd), 0);
        char view_buf[8192] = {0};
        ssize_t n = recv(ss_sock, view_buf, sizeof(view_buf)-1, 0);
        log_event(LOG_INFO, "[DEBUG] recv() from SS %d returned n=%zd", ss_id, n);
        if (n > 0) {
            view_buf[n] = '\0';
            log_event(LOG_INFO, "[DEBUG] VIEW response from SS %d: %s", ss_id, view_buf);
            char *saveptr = NULL;
            char *line = strtok_r(view_buf, "\n", &saveptr);
            while (line) {
                // skip header lines, empty lines, and non-filename lines
                if (line[0] == '\0' || line[0] == '-' || line[0] == '|' || strstr(line, "(no files found)") != NULL) {
                    line = strtok_r(NULL, "\n", &saveptr);
                    continue;
                }
                // For each file, fetch metadata using INFO (with authentication)
                char info_cmd[512];
                snprintf(info_cmd, sizeof(info_cmd), "USER:admin\nPASS:admin123\nCMD:INFO %s\n", line);
                log_event(LOG_INFO, "[DEBUG] Sending INFO for file: '%s' (cmd: %s)", line, info_cmd);
                int info_sock = socket(AF_INET, SOCK_STREAM, 0);
                if (info_sock < 0) { line = strtok_r(NULL, "\n", &saveptr); continue; }
                setsockopt(info_sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);
                setsockopt(info_sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof tv);
                if (connect(info_sock, (struct sockaddr*)&sa, sizeof(sa)) == 0) {
                    send(info_sock, info_cmd, strlen(info_cmd), 0);
                    char info_buf[2048] = {0};
                    ssize_t info_n = recv(info_sock, info_buf, sizeof(info_buf)-1, 0);
                    if (info_n > 0) {
                        info_buf[info_n] = '\0';
                        // Parse INFO response and fill FileMeta
                        FileMeta meta = {0};
                        strncpy(meta.name, line, sizeof(meta.name)-1);
                        meta.ss_ids[0] = ss_id;
                        meta.ss_count = 1;
                        // Parse fields from info_buf (simple parsing)
                        char *p = strstr(info_buf, "Owner          : ");
                        if (p) sscanf(p, "Owner          : %63[^\n]", meta.owner);
                        p = strstr(info_buf, "Created        : ");
                        if (p) {
                            char tbuf[64];
                            if (sscanf(p, "Created        : %63[^\n]", tbuf) == 1) {
                                struct tm tm; if (strptime(tbuf, "%Y-%m-%d %H:%M:%S", &tm)) meta.created_time = mktime(&tm);
                            }
                        }
                        p = strstr(info_buf, "Last Modified  : ");
                        if (p) {
                            char tbuf[64];
                            if (sscanf(p, "Last Modified  : %63[^\n]", tbuf) == 1) {
                                struct tm tm; if (strptime(tbuf, "%Y-%m-%d %H:%M:%S", &tm)) meta.last_modified = mktime(&tm);
                            }
                        }
                        p = strstr(info_buf, "Last Access    : ");
                        if (p) {
                            char tbuf[64];
                            if (sscanf(p, "Last Access    : %63[^\n]", tbuf) == 1) {
                                struct tm tm; if (strptime(tbuf, "%Y-%m-%d %H:%M:%S", &tm)) meta.last_accessed = mktime(&tm);
                            }
                        }
                        p = strstr(info_buf, "Read Access    : ");
                        if (p) sscanf(p, "Read Access    : %511[^\n]", meta.read_users);
                        p = strstr(info_buf, "Write Access   : ");
                        if (p) sscanf(p, "Write Access   : %511[^\n]", meta.write_users);
                        // Insert into hashmap
                        FileMeta *existing = file_index_get(&file_index, meta.name);
                        log_event(LOG_INFO, "[DEBUG] Inserting file into hashmap: '%s' (key)", meta.name);
                        if (!existing) {
                            FileMeta *newmeta = malloc(sizeof(FileMeta));
                            *newmeta = meta;
                            unsigned long h = hash_filename(meta.name) % file_index.num_buckets;
                            newmeta->next = file_index.buckets[h];
                            file_index.buckets[h] = newmeta;
                        } else {
                            // Update ss_ids if needed
                            int found = 0;
                            for (int i = 0; i < existing->ss_count; ++i) if (existing->ss_ids[i] == ss_id) { found = 1; break; }
                            if (!found && existing->ss_count < MAX_SS) existing->ss_ids[existing->ss_count++] = ss_id;
                        }
                    }
                }
                close(info_sock);
                line = strtok_r(NULL, "\n", &saveptr);
            }
        }
    }
    close(ss_sock);
}

// Add a storage server entry and return its id, or -1 on failure
static int add_storage_server(const char *ip, int nm_port, int client_port_from_reg, const char *files) {
    // Step 1: Ping all registered storage servers and remove any that are unreachable
    int i = 0;
    log_event(LOG_INFO, "Received storage server registration request from IP=%s, NM_PORT=%d, CLIENT_PORT=%d", ip, nm_port, client_port_from_reg);
    while (i < num_storage_servers) {
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) { ++i; continue; }
        struct sockaddr_in sa;
        sa.sin_family = AF_INET;
        sa.sin_port = htons(storage_servers[i].client_port);
        sa.sin_addr.s_addr = inet_addr(storage_servers[i].ip);
        struct timeval tv; tv.tv_sec = 0; tv.tv_usec = 300000; // 300ms timeout
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof tv);
        int res = connect(sock, (struct sockaddr*)&sa, sizeof(sa));
        close(sock);
        if (res < 0) {
            log_event(LOG_WARN, "Removing unreachable storage server: IP=%s, CLIENT_PORT=%d", storage_servers[i].ip, storage_servers[i].client_port);
            for (int j = i; j < num_storage_servers - 1; ++j) storage_servers[j] = storage_servers[j + 1];
            num_storage_servers--;
        } else {
            ++i;
        }
    }

    if (num_storage_servers >= MAX_SS) return -1;
    // Step 2: Assign lowest available id
    int used_ids[MAX_SS+1] = {0};
    for (int k = 0; k < num_storage_servers; ++k) {
        if (storage_servers[k].id > 0 && storage_servers[k].id <= MAX_SS) {
            used_ids[storage_servers[k].id] = 1;
        }
    }
    int id = 1;
    for (; id <= MAX_SS; ++id) {
        if (!used_ids[id]) break;
    }
    if (id > MAX_SS) return -1;

    int idx = num_storage_servers;
    storage_servers[idx].id = id;
    strncpy(storage_servers[idx].ip, ip ? ip : "127.0.0.1", sizeof(storage_servers[idx].ip)-1);
    storage_servers[idx].nm_port = nm_port;
    if (client_port_from_reg <= 0) {
        storage_servers[idx].client_port = STORAGE_SERVER_PORT + id;
    } else {
        storage_servers[idx].client_port = (client_port_from_reg == STORAGE_SERVER_PORT) ? (STORAGE_SERVER_PORT + id) : client_port_from_reg;
    }
    storage_servers[idx].last_seen = time(NULL);
    storage_servers[idx].active = 1;
    if (files) {
        strncpy(storage_servers[idx].files, files, sizeof(storage_servers[idx].files)-1);
    } else {
        storage_servers[idx].files[0] = '\0';
    }
    num_storage_servers++;
    // After registration, update file index from this storage server (moved to main loop after response)
    return id;
}

StorageServerInfo *find_ss_by_id(int id) {
    for (int i = 0; i < num_storage_servers; ++i) if (storage_servers[i].id == id) return &storage_servers[i];
    return NULL;
}

FileMeta *find_filemeta(const char *name) {
    return file_index_get(&file_index, name);
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

    // Initialize file index
    file_index_init(&file_index, 4096);

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

        char client_ip[64];
        unsigned short client_port = 0;
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
        client_port = ntohs(client_addr.sin_port);

        // Peek at the incoming data to detect registration messages
        char peek[8192];
        ssize_t peek_n = recv(client_sock, peek, sizeof(peek)-1, MSG_PEEK);
        if (peek_n < 0) peek_n = 0;
        peek[peek_n] = '\0';

        // If this is an authentication request, handle it in parent
        if (peek_n > 6 && strstr(peek, "TYPE:AUTH") == peek) {
            // read the full authentication message (consume it)
            char authbuf[8192];
            ssize_t rn = recv(client_sock, authbuf, sizeof(authbuf)-1, 0);
            if (rn < 0) rn = 0;
            authbuf[rn] = '\0';

            // parse authentication lines
            char *saveptr_auth = NULL;
            char *line = strtok_r(authbuf, "\n", &saveptr_auth);
            char username[64] = "";
            char password[64] = "";
            while (line) {
                if (strncmp(line, "USER:", 5) == 0) {
                    strncpy(username, line + 5, sizeof(username) - 1);
                } else if (strncmp(line, "PASS:", 5) == 0) {
                    strncpy(password, line + 5, sizeof(password) - 1);
                }
                line = strtok_r(NULL, "\n", &saveptr_auth);
            }

            // Verify credentials against storage/users.txt
            int authenticated = 0;
            FILE *users_file = fopen("storage/users.txt", "r");
            if (users_file) {
                char userline[256];
                while (fgets(userline, sizeof(userline), users_file)) {
                    userline[strcspn(userline, "\n")] = 0;
                    if (userline[0] == '#' || strlen(userline) == 0) continue;
                    char *colon = strchr(userline, ':');
                    if (colon) {
                        *colon = '\0';
                        char *file_user = userline;
                        char *file_pass = colon + 1;
                        if (strcmp(file_user, username) == 0 && strcmp(file_pass, password) == 0) {
                            authenticated = 1;
                            break;
                        }
                    }
                }
                fclose(users_file);
            }

            char auth_resp[128];
            if (authenticated) {
                snprintf(auth_resp, sizeof(auth_resp), "AUTH:SUCCESS\n");
                log_event(LOG_INFO, "Authentication SUCCESS for user '%s' from IP=%s:%u", username, client_ip, client_port);
            } else {
                snprintf(auth_resp, sizeof(auth_resp), "AUTH:FAILED\n");
                log_event(LOG_WARN, "Authentication FAILED for user '%s' from IP=%s:%u", username, client_ip, client_port);
            }
            send(client_sock, auth_resp, strlen(auth_resp), 0);
            close(client_sock);
            continue;
        }

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
            int client_port_reg = 0;
            char files[4096] = "";
            while (line) {
                if (strncmp(line, "IP:", 3) == 0) {
                    strncpy(ipstr, line + 3, sizeof(ipstr)-1);
                } else if (strncmp(line, "NM_PORT:", 8) == 0) {
                    nm_port = atoi(line + 8);
                } else if (strncmp(line, "CLIENT_PORT:", 12) == 0) {
                    client_port_reg = atoi(line + 12);
                } else if (strncmp(line, "FILES:", 6) == 0) {
                    strncpy(files, line + 6, sizeof(files)-1);
                }
                line = strtok_r(NULL, "\n", &saveptr);
            }

            log_event(LOG_INFO, "Received TYPE:REGISTER_SS from IP=%s:%u (reported IP=%s, NM_PORT=%d, CLIENT_PORT=%d)", client_ip, client_port, ipstr, nm_port, client_port_reg);
            int ss_id = add_storage_server(ipstr, nm_port, client_port_reg, files);
            char resp[128];
            if (ss_id >= 0) {
                snprintf(resp, sizeof(resp), "SS_ID:%d\n", ss_id);
                log_event(LOG_INFO, "Storage server registered: SS_ID=%d, IP=%s, NM_PORT=%d, CLIENT_PORT=%d", ss_id, ipstr, nm_port, client_port_reg);
            } else {
                snprintf(resp, sizeof(resp), "SS_ID:-1\n");
                log_event(LOG_ERROR, "Storage server registration FAILED for IP=%s, NM_PORT=%d, CLIENT_PORT=%d", ipstr, nm_port, client_port_reg);
            }
            send(client_sock, resp, strlen(resp), 0);
            close(client_sock);
            // Now update file index from this storage server (after response and close)
            if (ss_id >= 0) {
                usleep(200 * 1000); // 200ms sleep to allow SS to start listening
                // Use the actual registered port from storage_servers array
                StorageServerInfo *ssi = find_ss_by_id(ss_id);
                int update_port = (ssi) ? ssi->client_port : ((client_port_reg > 0) ? client_port_reg : (STORAGE_SERVER_PORT + ss_id));
                update_file_index_from_ss(ipstr, update_port, ss_id);
            }
            continue;
        }

        // Check if this is a CREATE or DELETE command - handle in parent without forking
        if (peek_n > 0) {
            // Parse to check for CREATE/DELETE commands
            char peek_copy[8192];
            strncpy(peek_copy, peek, sizeof(peek_copy)-1);
            peek_copy[peek_n] = '\0';
            
            // Extract username and command
            char peek_username[64] = "";
            char peek_password[64] = "";
            char peek_command[1024] = "";
            char *saveptr_peek = NULL;
            char *peek_line = strtok_r(peek_copy, "\n", &saveptr_peek);
            while (peek_line) {
                if (strncmp(peek_line, "USER:", 5) == 0) {
                    strncpy(peek_username, peek_line + 5, sizeof(peek_username) - 1);
                } else if (strncmp(peek_line, "PASS:", 5) == 0) {
                    strncpy(peek_password, peek_line + 5, sizeof(peek_password) - 1);
                } else if (strncmp(peek_line, "CMD:", 4) == 0) {
                    strncpy(peek_command, peek_line + 4, sizeof(peek_command) - 1);
                    break;
                }
                peek_line = strtok_r(NULL, "\n", &saveptr_peek);
            }
            
            // Check if it's CREATE or DELETE - handle synchronously in parent
            if (strncmp(peek_command, "CREATE ", 7) == 0) {
                char filename[256] = "";
                sscanf(peek_command + 7, "%255s", filename);
                if (filename[0] != '\0') {
                    // Consume the request
                    char reqbuf[8192];
                    recv(client_sock, reqbuf, sizeof(reqbuf)-1, 0);
                    
                    // Forward to storage server
                    int ss_id_target = -1;
                    FileMeta *existing_meta = find_filemeta(filename);
                    if (existing_meta && existing_meta->ss_count > 0) {
                        ss_id_target = existing_meta->ss_ids[0];
                    } else {
                        // Round-robin for new file
                        static int rr = 0;
                        for (int attempts=0; attempts< num_storage_servers; ++attempts) {
                            int idx = (rr + attempts) % num_storage_servers;
                            if (storage_servers[idx].active) { ss_id_target = storage_servers[idx].id; rr = (idx+1)%num_storage_servers; break; }
                        }
                    }
                    
                    if (ss_id_target >= 0) {
                        StorageServerInfo *ssi = find_ss_by_id(ss_id_target);
                        if (ssi) {
                            int storage_sock = socket(AF_INET, SOCK_STREAM, 0);
                            if (storage_sock >= 0) {
                                struct sockaddr_in sa_ss; sa_ss.sin_family = AF_INET; sa_ss.sin_port = htons(ssi->client_port); sa_ss.sin_addr.s_addr = inet_addr(ssi->ip);
                                if (connect(storage_sock, (struct sockaddr*)&sa_ss, sizeof(sa_ss)) == 0) {
                                    // Forward CREATE command
                                    char auth_cmd[8192];
                                    snprintf(auth_cmd, sizeof(auth_cmd), "USER:%s\nPASS:%s\nCMD:%s", peek_username, peek_password, peek_command);
                                    send(storage_sock, auth_cmd, strlen(auth_cmd), 0);
                                    
                                    // Read response
                                    char response[4096];
                                    ssize_t resp_n = recv(storage_sock, response, sizeof(response)-1, 0);
                                    if (resp_n > 0) {
                                        response[resp_n] = '\0';
                                        send(client_sock, response, resp_n, 0);
                                        
                                        // Check if successful, then update hashmap
                                        if (strstr(response, "Success") != NULL || strstr(response, "success") != NULL) {
                                            FileMeta *newmeta = malloc(sizeof(FileMeta));
                                            memset(newmeta, 0, sizeof(FileMeta));
                                            strncpy(newmeta->name, filename, sizeof(newmeta->name)-1);
                                            strncpy(newmeta->owner, peek_username, sizeof(newmeta->owner)-1);
                                            time_t now = time(NULL);
                                            newmeta->created_time = now;
                                            newmeta->last_modified = now;
                                            newmeta->last_accessed = now;
                                            newmeta->ss_ids[0] = ss_id_target;
                                            newmeta->ss_count = 1;
                                            snprintf(newmeta->read_users, sizeof(newmeta->read_users), "%s", peek_username);
                                            snprintf(newmeta->write_users, sizeof(newmeta->write_users), "%s", peek_username);
                                            unsigned long h = hash_filename(filename) % file_index.num_buckets;
                                            newmeta->next = file_index.buckets[h];
                                            file_index.buckets[h] = newmeta;
                                            log_event(LOG_INFO, "[PARENT] File '%s' created by '%s' and metadata added to index", filename, peek_username);
                                        }
                                    }
                                    close(storage_sock);
                                }
                            }
                        }
                    }
                    close(client_sock);
                    continue;
                }
            } else if (strncmp(peek_command, "DELETE ", 7) == 0) {
                char filename[256] = "";
                sscanf(peek_command + 7, "%255s", filename);
                if (filename[0] != '\0') {
                    // Consume the request
                    char reqbuf[8192];
                    recv(client_sock, reqbuf, sizeof(reqbuf)-1, 0);
                    
                    // Forward to storage server
                    FileMeta *meta = find_filemeta(filename);
                    if (meta && meta->ss_count > 0) {
                        StorageServerInfo *ssi = find_ss_by_id(meta->ss_ids[0]);
                        if (ssi) {
                            int storage_sock = socket(AF_INET, SOCK_STREAM, 0);
                            if (storage_sock >= 0) {
                                struct sockaddr_in sa_ss; sa_ss.sin_family = AF_INET; sa_ss.sin_port = htons(ssi->client_port); sa_ss.sin_addr.s_addr = inet_addr(ssi->ip);
                                if (connect(storage_sock, (struct sockaddr*)&sa_ss, sizeof(sa_ss)) == 0) {
                                    // Forward DELETE command
                                    char auth_cmd[8192];
                                    snprintf(auth_cmd, sizeof(auth_cmd), "USER:%s\nPASS:%s\nCMD:%s", peek_username, peek_password, peek_command);
                                    send(storage_sock, auth_cmd, strlen(auth_cmd), 0);
                                    
                                    // Read response
                                    char response[4096];
                                    ssize_t resp_n = recv(storage_sock, response, sizeof(response)-1, 0);
                                    if (resp_n > 0) {
                                        response[resp_n] = '\0';
                                        send(client_sock, response, resp_n, 0);
                                        
                                        // Check if successful, then remove from hashmap
                                        if (strstr(response, "Success") != NULL || strstr(response, "success") != NULL || strstr(response, "deleted") != NULL) {
                                            unsigned long h = hash_filename(filename) % file_index.num_buckets;
                                            FileMeta **bucket = &file_index.buckets[h];
                                            FileMeta *prev = NULL;
                                            FileMeta *curr = *bucket;
                                            while (curr) {
                                                if (strcmp(curr->name, filename) == 0) {
                                                    if (prev) {
                                                        prev->next = curr->next;
                                                    } else {
                                                        *bucket = curr->next;
                                                    }
                                                    free(curr);
                                                    log_event(LOG_INFO, "[PARENT] File '%s' deleted and removed from index", filename);
                                                    break;
                                                }
                                                prev = curr;
                                                curr = curr->next;
                                            }
                                        }
                                    }
                                    close(storage_sock);
                                }
                            }
                        }
                    }
                    close(client_sock);
                    continue;
                }
            } else if (strncmp(peek_command, "ADDACCESS ", 10) == 0) {
                // Parse: ADDACCESS -R|-W <filename> <target_username>
                char flag[8], filename[256], target_user[64];
                if (sscanf(peek_command + 10, "%s %s %s", flag, filename, target_user) == 3) {
                    // Consume the request
                    char reqbuf[8192];
                    recv(client_sock, reqbuf, sizeof(reqbuf)-1, 0);
                    
                    // Check if requester is the owner
                    FileMeta *meta = find_filemeta(filename);
                    char response[512];
                    if (!meta) {
                        snprintf(response, sizeof(response), "Error: File '%s' not found\n", filename);
                        send(client_sock, response, strlen(response), 0);
                    } else if (strcmp(meta->owner, peek_username) != 0) {
                        snprintf(response, sizeof(response), "Error: Only the owner can grant access to '%s'\n", filename);
                        send(client_sock, response, strlen(response), 0);
                    } else {
                        // Update metadata in hashmap
                        if (strcmp(flag, "-R") == 0) {
                            // Add to read_users if not already present
                            if (strstr(meta->read_users, target_user) == NULL) {
                                if (strlen(meta->read_users) > 0) strcat(meta->read_users, ",");
                                strncat(meta->read_users, target_user, sizeof(meta->read_users) - strlen(meta->read_users) - 1);
                                snprintf(response, sizeof(response), "Success: Read access granted to '%s' for file '%s'\n", target_user, filename);
                                log_event(LOG_INFO, "[PARENT] Read access granted to '%s' for file '%s'", target_user, filename);
                            } else {
                                snprintf(response, sizeof(response), "Info: User '%s' already has read access to '%s'\n", target_user, filename);
                            }
                        } else if (strcmp(flag, "-W") == 0) {
                            // Add to write_users if not already present
                            if (strstr(meta->write_users, target_user) == NULL) {
                                if (strlen(meta->write_users) > 0) strcat(meta->write_users, ",");
                                strncat(meta->write_users, target_user, sizeof(meta->write_users) - strlen(meta->write_users) - 1);
                                snprintf(response, sizeof(response), "Success: Write access granted to '%s' for file '%s'\n", target_user, filename);
                                log_event(LOG_INFO, "[PARENT] Write access granted to '%s' for file '%s'", target_user, filename);
                            } else {
                                snprintf(response, sizeof(response), "Info: User '%s' already has write access to '%s'\n", target_user, filename);
                            }
                        } else {
                            snprintf(response, sizeof(response), "Error: Invalid flag '%s'. Use -R for read or -W for write\n", flag);
                        }
                        send(client_sock, response, strlen(response), 0);
                    }
                    close(client_sock);
                    continue;
                } else {
                    // Invalid format, let it fall through to child process which will send error
                }
            } else if (strncmp(peek_command, "REMACCESS ", 10) == 0) {
                // Parse: REMACCESS <filename> <target_username>
                char filename[256], target_user[64];
                if (sscanf(peek_command + 10, "%s %s", filename, target_user) == 2) {
                    // Consume the request
                    char reqbuf[8192];
                    recv(client_sock, reqbuf, sizeof(reqbuf)-1, 0);
                    
                    // Check if requester is the owner
                    FileMeta *meta = find_filemeta(filename);
                    char response[512];
                    if (!meta) {
                        snprintf(response, sizeof(response), "Error: File '%s' not found\n", filename);
                        send(client_sock, response, strlen(response), 0);
                    } else if (strcmp(meta->owner, peek_username) != 0) {
                        snprintf(response, sizeof(response), "Error: Only the owner can revoke access to '%s'\n", filename);
                        send(client_sock, response, strlen(response), 0);
                    } else if (strcmp(target_user, peek_username) == 0) {
                        snprintf(response, sizeof(response), "Error: Cannot revoke owner's access\n");
                        send(client_sock, response, strlen(response), 0);
                    } else {
                        // Remove from both read_users and write_users
                        char new_read[512] = "", new_write[512] = "";
                        
                        // Remove from read_users
                        char *read_copy = strdup(meta->read_users);
                        if (read_copy) {
                            char *saveptr_read = NULL;
                            char *user = strtok_r(read_copy, ",", &saveptr_read);
                            while (user) {
                                if (strcmp(user, target_user) != 0) {
                                    if (strlen(new_read) > 0) strcat(new_read, ",");
                                    strcat(new_read, user);
                                }
                                user = strtok_r(NULL, ",", &saveptr_read);
                            }
                            free(read_copy);
                        }
                        
                        // Remove from write_users
                        char *write_copy = strdup(meta->write_users);
                        if (write_copy) {
                            char *saveptr_write = NULL;
                            char *user = strtok_r(write_copy, ",", &saveptr_write);
                            while (user) {
                                if (strcmp(user, target_user) != 0) {
                                    if (strlen(new_write) > 0) strcat(new_write, ",");
                                    strcat(new_write, user);
                                }
                                user = strtok_r(NULL, ",", &saveptr_write);
                            }
                            free(write_copy);
                        }
                        
                        // Update metadata
                        strncpy(meta->read_users, new_read, sizeof(meta->read_users) - 1);
                        strncpy(meta->write_users, new_write, sizeof(meta->write_users) - 1);
                        
                        snprintf(response, sizeof(response), "Success: All access revoked for '%s' on file '%s'\n", target_user, filename);
                        log_event(LOG_INFO, "[PARENT] All access revoked for '%s' on file '%s'", target_user, filename);
                        send(client_sock, response, strlen(response), 0);
                    }
                    close(client_sock);
                    continue;
                } else {
                    // Invalid format, let it fall through to child process which will send error
                }
            }
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

        // child: read command first, then choose storage server(s)
        close(listen_fd);

        char buf[4096];
        ssize_t n = recv(client_sock, buf, sizeof(buf) - 1, 0);
        if (n < 0) n = 0;
        buf[n] = '\0';
        // Trim trailing CR/LF
        while (n > 0 && (buf[n-1] == '\n' || buf[n-1] == '\r')) { buf[--n] = '\0'; }

        if (n == 0) {
            const char *msg = "Error: Empty command\n";
            send(client_sock, msg, strlen(msg), 0);
            close(client_sock);
            exit(0);
        }

        // Parse authentication credentials
        char username[64] = "", password[64] = "", command[4096] = "";
        char *line_ptr = buf;
        char *saveptr_auth = NULL;
        char *auth_line = strtok_r(line_ptr, "\n", &saveptr_auth);
        while (auth_line) {
            if (strncmp(auth_line, "USER:", 5) == 0) {
                strncpy(username, auth_line + 5, sizeof(username) - 1);
            } else if (strncmp(auth_line, "PASS:", 5) == 0) {
                strncpy(password, auth_line + 5, sizeof(password) - 1);
            } else if (strncmp(auth_line, "CMD:", 4) == 0) {
                strncpy(command, auth_line + 4, sizeof(command) - 1);
                break; // command is the last line we care about
            }
            auth_line = strtok_r(NULL, "\n", &saveptr_auth);
        }

        // Verify credentials against storage/users.txt
        int authenticated = 0;
        FILE *users_file = fopen("storage/users.txt", "r");
        if (users_file) {
            char line[256];
            while (fgets(line, sizeof(line), users_file)) {
                line[strcspn(line, "\n")] = 0;
                if (line[0] == '#' || strlen(line) == 0) continue; // skip comments/empty
                char *colon = strchr(line, ':');
                if (colon) {
                    *colon = '\0';
                    char *file_user = line;
                    char *file_pass = colon + 1;
                    if (strcmp(file_user, username) == 0 && strcmp(file_pass, password) == 0) {
                        authenticated = 1;
                        break;
                    }
                }
            }
            fclose(users_file);
        }

        if (!authenticated && strlen(username) > 0) {
            const char *msg = "Error: Authentication failed. Invalid username or password.\n";
            send(client_sock, msg, strlen(msg), 0);
            close(client_sock);
            exit(0);
        }

        // Use command from here on (replace buf references)
        strncpy(buf, command, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
        n = strlen(buf);



        // ===== ADD THE EXEC HANDLING CODE HERE =====
        if (strncmp(buf, "EXEC ", 5) == 0) {
            char filename[256];
            if (sscanf(buf + 5, "%255s", filename) != 1) {
                const char *msg = "Error: EXEC requires a filename\n";
                send(client_sock, msg, strlen(msg), 0);
                close(client_sock);
                exit(0);
            }
            
            // Find storage server for the file
            int ss_id = -1;
            FileMeta *meta = find_filemeta(filename);
            if (meta && meta->ss_count > 0) {
                for (int i = 0; i < meta->ss_count; ++i) {
                    StorageServerInfo *ssi = find_ss_by_id(meta->ss_ids[i]);
                    if (ssi && ssi->active) { 
                        ss_id = ssi->id; 
                        break; 
                    }
                }
            }
            
            if (ss_id < 0) {
                // Fallback to first active server
                for (int i = 0; i < num_storage_servers; ++i) {
                    if (storage_servers[i].active) { 
                        ss_id = storage_servers[i].id; 
                        break; 
                    }
                }
            }
            
            if (ss_id < 0) {
                const char *msg = "Error: No storage server available\n";
                send(client_sock, msg, strlen(msg), 0);
                close(client_sock);
                exit(0);
            }
            
            StorageServerInfo *ssi = find_ss_by_id(ss_id);
            int storage_sock = socket(AF_INET, SOCK_STREAM, 0);
            if (storage_sock < 0) {
                const char *msg = "Error: socket failure\n";
                send(client_sock, msg, strlen(msg), 0);
                close(client_sock);
                exit(0);
            }
            
            struct sockaddr_in sa_ss; 
            sa_ss.sin_family = AF_INET; 
            sa_ss.sin_port = htons(ssi->client_port); 
            sa_ss.sin_addr.s_addr = inet_addr(ssi->ip);
            
            if (connect(storage_sock, (struct sockaddr*)&sa_ss, sizeof(sa_ss)) < 0) {
                const char *msg = "Error: connect to storage failed\n";
                send(client_sock, msg, strlen(msg), 0);
                close(storage_sock);
                close(client_sock);
                exit(0);
            }
            
            // Send READ command to fetch file content
            char read_cmd[512]; 
            snprintf(read_cmd, sizeof(read_cmd), "READ %s", filename);
            char auth_read_cmd[8192];
            snprintf(auth_read_cmd, sizeof(auth_read_cmd), "USER:%s\nPASS:%s\nCMD:%s", username, password, read_cmd);
            send(storage_sock, auth_read_cmd, strlen(auth_read_cmd), 0);
            
            // Read file content from storage server
            char *file_buf = NULL; 
            size_t cap = 0, len = 0; 
            char rtmp[4096]; 
            ssize_t rr;
            
            while ((rr = recv(storage_sock, rtmp, sizeof(rtmp), 0)) > 0) {
                size_t rr_size = (size_t)rr;
                if (len + rr_size + 1 > cap) { 
                    size_t newcap = (cap == 0) ? (rr_size + 1) : (cap * 2 + rr_size + 1); 
                    char *nb = realloc(file_buf, newcap); 
                    if (!nb) break; 
                    file_buf = nb; 
                    cap = newcap; 
                }
                memcpy(file_buf + len, rtmp, rr); 
                len += rr;
            }
            
            if (file_buf) file_buf[len] = '\0';
            close(storage_sock);
            
            if (!file_buf || len == 0) {
                const char *fmt = "Error: Could not read file '%s' or empty\n"; 
                char msg[512]; 
                snprintf(msg, sizeof(msg), fmt, filename); 
                send(client_sock, msg, strlen(msg), 0); 
                free(file_buf); 
                close(client_sock); 
                exit(0);
            }
            
            // Execute each line and send output to client
            char *saveptr2 = NULL; 
            char *line = strtok_r(file_buf, "\n", &saveptr2);
            while (line) { 
                // Trim leading whitespace
                while (*line == ' ' || *line == '\t') line++; 
                
                // Skip empty lines and markdown fences
                if (*line && strncmp(line, "```", 3) != 0) { 
                    FILE *fp = popen(line, "r"); 
                    if (!fp) { 
                        char emsg[512]; 
                        snprintf(emsg, sizeof(emsg), "ERROR: Failed to execute: %s\n", line); 
                        send(client_sock, emsg, strlen(emsg), 0);
                    } else { 
                        char ob[1024]; 
                        while (fgets(ob, sizeof(ob), fp)) 
                            send(client_sock, ob, strlen(ob), 0); 
                        pclose(fp);
                    }
                }
                line = strtok_r(NULL, "\n", &saveptr2);
            }
            
            free(file_buf);
            close(client_sock);
            exit(0);
        }


        if (strcmp(buf, "LIST") == 0) {
            list_users(client_sock, username, client_ip, client_port);
            close(client_sock);
            exit(0);
        }



        // VIEW must go to all storage servers and aggregate
        if (strncmp(buf, "VIEW", 4) == 0) {
            char aggregate[65536];
            aggregate[0] = '\0';
            for (int i = 0; i < num_storage_servers; ++i) {
                if (!storage_servers[i].active) continue;
                int ss_port = storage_servers[i].client_port;
                int ss_sock = socket(AF_INET, SOCK_STREAM, 0);
                if (ss_sock < 0) continue;
                struct sockaddr_in sa_ss; sa_ss.sin_family = AF_INET; sa_ss.sin_port = htons(ss_port); sa_ss.sin_addr.s_addr = inet_addr(storage_servers[i].ip);
                if (connect(ss_sock, (struct sockaddr*)&sa_ss, sizeof(sa_ss)) < 0) { close(ss_sock); continue; }
                // send VIEW command with credentials
                char auth_view_cmd[8192];
                snprintf(auth_view_cmd, sizeof(auth_view_cmd), "USER:%s\nPASS:%s\nCMD:%s", username, password, buf);
                send(ss_sock, auth_view_cmd, strlen(auth_view_cmd), 0);
                // read response
                char rbuf[4096]; ssize_t r;
                strcat(aggregate, "\n--- StorageServer ");
                char hdr[64]; snprintf(hdr, sizeof(hdr), "%d (port %d) ---\n", storage_servers[i].id, ss_port); strcat(aggregate, hdr);
                while ((r = recv(ss_sock, rbuf, sizeof(rbuf)-1, 0)) > 0) {
                    rbuf[r] = '\0';
                    if (strlen(aggregate) + r + 1 < sizeof(aggregate)) strcat(aggregate, rbuf);
                }
                close(ss_sock);
            }
            if (aggregate[0] == '\0') strcpy(aggregate, "(No active storage servers or no data)\n");
            send(client_sock, aggregate, strlen(aggregate), 0);
            close(client_sock);
            exit(0);
        }


        // Other file-based commands: choose storage server and forward
        const char *cmds_with_file[] = {"READ", "STREAM", "DELETE", "WRITE", "CREATE"};
        int is_file_cmd = 0; const char *file_part = NULL; char filename[256]; filename[0]='\0';
        for (size_t i=0;i<sizeof(cmds_with_file)/sizeof(cmds_with_file[0]);++i) {
            size_t clen = strlen(cmds_with_file[i]);
            if (strncmp(buf, cmds_with_file[i], clen)==0 && (buf[clen]==' ' || buf[clen]=='\0')) { is_file_cmd = 1; file_part = buf + clen; break; }
        }
        int ss_id_target = -1;
        if (is_file_cmd) {
            // extract filename (may have extra arguments for WRITE)
            if (sscanf(file_part, " %255s", filename) == 1) {
                // For CREATE if file doesn't exist yet choose a server via simple round-robin
                FileMeta *meta = find_filemeta(filename);
                if (meta && meta->ss_count > 0) {
                    for (int i = 0; i < meta->ss_count; ++i) {
                        StorageServerInfo *ssi = find_ss_by_id(meta->ss_ids[i]);
                        if (ssi && ssi->active) { ss_id_target = ssi->id; break; }
                    }
                } else if (strncmp(buf, "CREATE", 6)==0) {
                    // round-robin
                    static int rr = 0;
                    for (int attempts=0; attempts< num_storage_servers; ++attempts) {
                        int idx = (rr + attempts) % num_storage_servers;
                        if (storage_servers[idx].active) { ss_id_target = storage_servers[idx].id; rr = (idx+1)%num_storage_servers; break; }
                    }
                } else {
                    // fallback: first active storage server
                    for (int i = 0; i < num_storage_servers; ++i) {
                        if (storage_servers[i].active) { ss_id_target = storage_servers[i].id; break; }
                    }
                }
            }
        }
        if (ss_id_target < 0) {
            // fallback for non file commands or no mapping
            for (int i=0;i<num_storage_servers;++i) if (storage_servers[i].active){ ss_id_target = storage_servers[i].id; break; }
        }
        if (ss_id_target < 0) {
            const char *msg = "Error: No storage server available\n"; send(client_sock, msg, strlen(msg),0); close(client_sock); exit(0);
        }
        StorageServerInfo *ssi = find_ss_by_id(ss_id_target);
        int storage_sock = socket(AF_INET, SOCK_STREAM, 0);
        if (storage_sock < 0) { const char *msg = "Error: socket failure\n"; send(client_sock,msg,strlen(msg),0); close(client_sock); exit(0);}        
        struct sockaddr_in sa_ss; sa_ss.sin_family = AF_INET; sa_ss.sin_port = htons(ssi->client_port); sa_ss.sin_addr.s_addr = inet_addr(ssi->ip);
        if (connect(storage_sock, (struct sockaddr*)&sa_ss, sizeof(sa_ss)) < 0) { const char *msg = "Error: connect to storage failed\n"; send(client_sock,msg,strlen(msg),0); close(storage_sock); close(client_sock); exit(0);}        

        // Forward original command
        // Forward original command with authentication
        char auth_cmd[8192];
        snprintf(auth_cmd, sizeof(auth_cmd), "USER:%s\nPASS:%s\nCMD:%s", username, password, buf);
        send(storage_sock, auth_cmd, strlen(auth_cmd), 0);

        // Handle INFO command in name server using hashmap
        if (strncmp(buf, "INFO", 4) == 0) {
            // Extract filename
            char info_filename[256] = "";
            sscanf(buf + 4, "%255s", info_filename);
            if (info_filename[0] == '\0') {
                const char *msg = "Error: Please specify a filename\n";
                send(client_sock, msg, strlen(msg), 0);
                close(storage_sock);
                close(client_sock);
                exit(0);
            }
            log_event(LOG_INFO, "[DEBUG] Looking up file in hashmap: '%s' (key)", info_filename);
            FileMeta *meta = find_filemeta(info_filename);
            if (!meta) {
                const char *msg = "Error: File not found in name server index\n";
                send(client_sock, msg, strlen(msg), 0);
                close(storage_sock);
                close(client_sock);
                exit(0);
            }
            // Compose info response from meta
            char response[2048];
            char created_str[64] = "N/A", modified_str[64] = "N/A", accessed_str[64] = "N/A";
            if (meta->created_time) strftime(created_str, sizeof(created_str), "%Y-%m-%d %H:%M:%S", localtime(&meta->created_time));
            if (meta->last_modified) strftime(modified_str, sizeof(modified_str), "%Y-%m-%d %H:%M:%S", localtime(&meta->last_modified));
            if (meta->last_accessed) strftime(accessed_str, sizeof(accessed_str), "%Y-%m-%d %H:%M:%S", localtime(&meta->last_accessed));
            snprintf(response, sizeof(response),
                "------------------- FILE INFO -------------------\n"
                "File Name      : %s\n"
                "Owner          : %s\n"
                "Created        : %s\n"
                "Last Modified  : %s\n"
                "Last Access    : %s\n"
                "Read Access    : %s\n"
                "Write Access   : %s\n"
                "Storage Servers: ",
                meta->name, meta->owner, created_str, modified_str, accessed_str, meta->read_users, meta->write_users);
            send(client_sock, response, strlen(response), 0);
            // List storage servers
            char sslist[256] = "";
            for (int i = 0; i < meta->ss_count; ++i) {
                char tmp[32];
                snprintf(tmp, sizeof(tmp), "%d%s", meta->ss_ids[i], (i < meta->ss_count-1)?", ":"\n");
                strcat(sslist, tmp);
            }
            send(client_sock, sslist, strlen(sslist), 0);
            close(storage_sock);
            close(client_sock);
            exit(0);
        } else if (strncmp(buf, "WRITE", 5) == 0) {
            // WRITE command needs bidirectional proxying for interactive session
            proxy_bidirectional(client_sock, storage_sock);
            close(storage_sock);
            close(client_sock);
            exit(0);
        } else {
            // Simple response relay until storage closes (for non-interactive commands)
            char relay[4096]; 
            ssize_t rcv;
            while ((rcv = recv(storage_sock, relay, sizeof(relay)-1, 0)) > 0) { 
                relay[rcv]='\0'; 
                send(client_sock, relay, strlen(relay), 0); 
            }
        }


        close(storage_sock);
        close(client_sock);
        exit(0);

        // If this is an EXEC command, handle it on the name server:
        // 1) ask storage server for the file content (send READ <filename>)
        // 2) execute each non-empty line locally via popen()
        // 3) stream outputs back to the client

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