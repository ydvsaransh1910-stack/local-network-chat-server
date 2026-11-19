#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/select.h>

#define INITIAL_CAP 4
#define BUF_SIZE 1024
#define NAME_LEN 32
#define LOG_FILE "chat_history.log"

typedef struct {
    int fd;
    char username[NAME_LEN];
} Client;

Client *clients = NULL;
int client_count = 0;
int client_cap = 0;

void log_message(const char *line) {
    FILE *fp = fopen(LOG_FILE, "a");
    if (fp) {
        fprintf(fp, "%s\n", line);
        fclose(fp);
    }
}

void send_all_bytes(int fd, const char *msg) {
    size_t len = strlen(msg);
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(fd, msg + sent, len - sent, 0);
        if (n <= 0) break;
        sent += (size_t)n;
    }
}

void broadcast(const char *msg, int exclude_fd) {
    for (int i = 0; i < client_count; i++) {
        if (clients[i].fd != exclude_fd) {
            send_all_bytes(clients[i].fd, msg);
        }
    }
}

int add_client(int fd, int id_num) {
    if (client_count == client_cap) {
        int new_cap = (client_cap == 0) ? INITIAL_CAP : client_cap * 2;
        Client *temp = realloc(clients, (size_t)new_cap * sizeof(Client));
        if (!temp) {
            perror("realloc failed");
            return -1;
        }
        clients = temp;
        client_cap = new_cap;
    }
    clients[client_count].fd = fd;
    snprintf(clients[client_count].username, NAME_LEN, "user%d", id_num);
    client_count++;
    return 0;
}

int find_client_index(int fd) {
    for (int i = 0; i < client_count; i++) {
        if (clients[i].fd == fd) return i;
    }
    return -1;
}

int find_client_by_name(const char *name) {
    for (int i = 0; i < client_count; i++) {
        if (strcmp(clients[i].username, name) == 0) return i;
    }
    return -1;
}

void remove_client_at(int idx) {
    if (idx < 0 || idx >= client_count) return;
    close(clients[idx].fd);
    for (int i = idx; i < client_count - 1; i++) {
        clients[i] = clients[i + 1];
    }
    client_count--;
}

void handle_client_command(int idx, char *input) {
    char out[BUF_SIZE + NAME_LEN + 64];
    char log_buf[BUF_SIZE + NAME_LEN + 64];

    if (strncmp(input, "/nick ", 6) == 0) {
        char *new_name = input + 6;
        while (*new_name == ' ') new_name++;
        if (strlen(new_name) == 0 || strlen(new_name) >= NAME_LEN) {
            send_all_bytes(clients[idx].fd, "Invalid nickname length.\n");
            return;
        }
        char old_name[NAME_LEN];
        strncpy(old_name, clients[idx].username, NAME_LEN);
        strncpy(clients[idx].username, new_name, NAME_LEN - 1);
        clients[idx].username[NAME_LEN - 1] = '\0';

        snprintf(out, sizeof(out), "* %s is now known as %s\n", old_name, clients[idx].username);
        broadcast(out, -1);
        snprintf(log_buf, sizeof(log_buf), "[NICK] %s -> %s", old_name, clients[idx].username);
        log_message(log_buf);
    } else if (strcmp(input, "/list") == 0) {
        send_all_bytes(clients[idx].fd, "--- Online Users ---\n");
        for (int i = 0; i < client_count; i++) {
            snprintf(out, sizeof(out), " - %s\n", clients[i].username);
            send_all_bytes(clients[idx].fd, out);
        }
    } else if (strncmp(input, "/msg ", 5) == 0) {
        char *rest = input + 5;
        char *space = strchr(rest, ' ');
        if (!space) {
            send_all_bytes(clients[idx].fd, "Usage: /msg <username> <message>\n");
            return;
        }
        *space = '\0';
        char *target_name = rest;
        char *pm_text = space + 1;
        while (*pm_text == ' ') pm_text++;
        if (*pm_text == '\0') {
            send_all_bytes(clients[idx].fd, "Message cannot be empty.\n");
            return;
        }
        int target_idx = find_client_by_name(target_name);
        if (target_idx == -1) {
            send_all_bytes(clients[idx].fd, "User not found.\n");
            return;
        }
        snprintf(out, sizeof(out), "[PM from %s]: %s\n", clients[idx].username, pm_text);
        send_all_bytes(clients[target_idx].fd, out);
        snprintf(out, sizeof(out), "[PM to %s]: %s\n", clients[target_idx].username, pm_text);
        send_all_bytes(clients[idx].fd, out);
        snprintf(log_buf, sizeof(log_buf), "[PM] %s -> %s: %s", clients[idx].username, clients[target_idx].username, pm_text);
        log_message(log_buf);
    } else if (strcmp(input, "/quit") == 0) {
        char gone_name[NAME_LEN];
        strncpy(gone_name, clients[idx].username, NAME_LEN);
        snprintf(out, sizeof(out), "* %s left the chat.\n", gone_name);
        remove_client_at(idx);
        broadcast(out, -1);
        snprintf(log_buf, sizeof(log_buf), "[LEAVE] %s disconnected", gone_name);
        log_message(log_buf);
    } else {
        snprintf(out, sizeof(out), "[%s]: %s\n", clients[idx].username, input);
        broadcast(out, clients[idx].fd);
        snprintf(log_buf, sizeof(log_buf), "[MSG] %s: %s", clients[idx].username, input);
        log_message(log_buf);
    }
}

void cleanup_server(int server_fd) {
    for (int i = 0; i < client_count; i++) {
        close(clients[i].fd);
    }
    free(clients);
    clients = NULL;
    client_count = 0;
    client_cap = 0;
    if (server_fd >= 0) close(server_fd);
}

int main(int argc, char *argv[]) {
    int port = (argc > 1) ? atoi(argv[1]) : 8080;

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        return 1;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons((uint16_t)port);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(server_fd);
        return 1;
    }

    if (listen(server_fd, 16) < 0) {
        perror("listen");
        close(server_fd);
        return 1;
    }

    printf("Chat server listening on port %d...\n", port);
    printf("Type '/logs' in server terminal to view chat history, or '/exit' to stop.\n");
    fflush(stdout);

    int next_id = 1;
    char buf[BUF_SIZE];

    while (1) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);
        FD_SET(server_fd, &readfds);
        int max_fd = (STDIN_FILENO > server_fd) ? STDIN_FILENO : server_fd;

        for (int i = 0; i < client_count; i++) {
            FD_SET(clients[i].fd, &readfds);
            if (clients[i].fd > max_fd) max_fd = clients[i].fd;
        }

        int activity = select(max_fd + 1, &readfds, NULL, NULL, NULL);
        if (activity < 0) {
            if (errno == EINTR) continue;
            perror("select");
            break;
        }

        /* Admin commands on server stdin */
        if (FD_ISSET(STDIN_FILENO, &readfds)) {
            if (fgets(buf, sizeof(buf), stdin)) {
                buf[strcspn(buf, "\r\n")] = '\0';
                if (strcmp(buf, "/logs") == 0) {
                    FILE *fp = fopen(LOG_FILE, "r");
                    printf("=== Chat History ===\n");
                    if (fp) {
                        char line[BUF_SIZE];
                        while (fgets(line, sizeof(line), fp)) {
                            printf("%s", line);
                        }
                        fclose(fp);
                    } else {
                        printf("(No logs yet)\n");
                    }
                    printf("====================\n");
                    fflush(stdout);
                } else if (strcmp(buf, "/exit") == 0) {
                    break;
                }
            }
        }

        /* New incoming TCP connection */
        if (FD_ISSET(server_fd, &readfds)) {
            struct sockaddr_in cli_addr;
            socklen_t cli_len = sizeof(cli_addr);
            int client_fd = accept(server_fd, (struct sockaddr *)&cli_addr, &cli_len);
            if (client_fd >= 0) {
                if (add_client(client_fd, next_id++) == 0) {
                    char welcome[128];
                    snprintf(welcome, sizeof(welcome),
                             "Connected! Your username is %s. Commands: /nick, /list, /msg, /quit\n",
                             clients[client_count - 1].username);
                    send_all_bytes(client_fd, welcome);

                    char join_msg[128];
                    snprintf(join_msg, sizeof(join_msg), "* %s joined the chat.\n",
                             clients[client_count - 1].username);
                    broadcast(join_msg, client_fd);
                    log_message(join_msg);
                } else {
                    close(client_fd);
                }
            }
        }

        /* Existing client sockets */
        for (int i = 0; i < client_count; i++) {
            int cfd = clients[i].fd;
            if (FD_ISSET(cfd, &readfds)) {
                ssize_t n = recv(cfd, buf, sizeof(buf) - 1, 0);
                if (n <= 0) {
                    char gone_name[NAME_LEN];
                    strncpy(gone_name, clients[i].username, NAME_LEN);
                    char drop_msg[128];
                    snprintf(drop_msg, sizeof(drop_msg), "* %s disconnected.\n", gone_name);
                    remove_client_at(i);
                    broadcast(drop_msg, -1);
                    log_message(drop_msg);
                    i--;
                } else {
                    buf[n] = '\0';
                    buf[strcspn(buf, "\r\n")] = '\0';
                    if (strlen(buf) > 0) {
                        handle_client_command(i, buf);
                        /* If /quit removed this client, shift index back */
                        if (i >= client_count || clients[i].fd != cfd) {
                            i--;
                        }
                    }
                }
            }
        }
    }

    cleanup_server(server_fd);
    return 0;
}