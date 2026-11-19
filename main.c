#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <conio.h>
#else
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <errno.h>
#endif

#define BUFFER_SIZE 2048
#define USERNAME_LEN 32

#ifdef _WIN32
typedef SOCKET socket_t;
#define socket_close(sock) closesocket(sock)
#else
typedef int socket_t;
#define socket_close(sock) close(sock)
#define SOCKET_ERROR (-1)
#endif

void trim_newline(char *str) {
    int len = strlen(str);
    while (len > 0 && (str[len - 1] == '\n' || str[len - 1] == '\r')) {
        str[len - 1] = '\0';
        len--;
    }
}

int main() {
    char server_ip[64];
    int server_port;
    char username[USERNAME_LEN];

#ifdef _WIN32
    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        fprintf(stderr, "Winsock initialization failed\n");
        return 1;
    }
#endif

    printf("===========================================\n");
    printf("        LOCAL NETWORK CHAT CLIENT          \n");
    printf("===========================================\n");
    printf("Enter Server IP address (e.g. 127.0.0.1): ");
    scanf("%63s", server_ip);

    printf("Enter Server Port number: ");
    if (scanf("%d", &server_port) != 1) return 1;
    if (server_port < 1 || server_port > 65535) {
        fprintf(stderr, "Invalid port number\n");
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }

    printf("Enter your desired Username: ");
    if (scanf("%31s", username) != 1) {
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }
    while (getchar() != '\n');

    socket_t sock = socket(AF_INET, SOCK_STREAM, 0);
    if (
#ifdef _WIN32
        sock == INVALID_SOCKET
#else
        sock < 0
#endif
    ) {
        perror("Socket creation failed");
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);

#ifdef _WIN32
    server_addr.sin_addr.s_addr = inet_addr(server_ip);
    if (server_addr.sin_addr.s_addr == INADDR_NONE) {
#else
    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0) {
#endif
        perror("Invalid IP address");
        socket_close(sock);
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }

    printf("Connecting to %s:%d ...\n", server_ip, server_port);
    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        perror("Connection failed");
        socket_close(sock);
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }

    printf("Connected successfully!\n");
    printf("Commands available:\n");
    printf("  /nick <new_name>          - Change nickname\n");
    printf("  /msg <username> <message> - Send a private message\n");
    printf("  /list                     - List online users\n");
    printf("  /quit                     - Exit chat\n\n");

    char initial_nick_cmd[64];
    snprintf(initial_nick_cmd, sizeof(initial_nick_cmd), "/nick %s\n", username);
    send(sock, initial_nick_cmd, strlen(initial_nick_cmd), 0);

    char send_buffer[BUFFER_SIZE];
    char recv_buffer[BUFFER_SIZE];

#ifdef _WIN32
    u_long nonblocking = 1;
    if (ioctlsocket(sock, FIONBIO, &nonblocking) == SOCKET_ERROR) {
        perror("Could not configure socket");
        socket_close(sock);
        WSACleanup();
        return 1;
    }
#else
    fd_set read_fds;
    while (1) {
        FD_ZERO(&read_fds);
        FD_SET(STDIN_FILENO, &read_fds);
        FD_SET(sock, &read_fds);

        int max_fd = (STDIN_FILENO > sock) ? STDIN_FILENO : sock;
        int activity = select(max_fd + 1, &read_fds, NULL, NULL, NULL);

        if (activity < 0) {
            perror("select error");
            break;
        }

        if (FD_ISSET(sock, &read_fds)) {
            memset(recv_buffer, 0, sizeof(recv_buffer));
            int bytes_read = recv(sock, recv_buffer, sizeof(recv_buffer) - 1, 0);
            if (bytes_read <= 0) {
                printf("\n[Disconnected from server]\n");
                break;
            }
            printf("%s", recv_buffer);
            fflush(stdout);
        }

        if (FD_ISSET(STDIN_FILENO, &read_fds)) {
            if (fgets(send_buffer, sizeof(send_buffer), stdin)) {
                trim_newline(send_buffer);
                if (strlen(send_buffer) == 0) continue;

                strcat(send_buffer, "\n");
                send(sock, send_buffer, strlen(send_buffer), 0);

                if (strncmp(send_buffer, "/quit", 5) == 0) {
                    printf("Leaving chat...\n");
                    break;
                }
            }
        }
    }
#endif

#ifdef _WIN32
    while (1) {
        int bytes_read = recv(sock, recv_buffer, sizeof(recv_buffer) - 1, 0);
        if (bytes_read == SOCKET_ERROR) {
            int error = WSAGetLastError();
            if (error != WSAEWOULDBLOCK) {
                fprintf(stderr, "\n[Disconnected from server]\n");
                break;
            }
        } else if (bytes_read == 0) {
            printf("\n[Disconnected from server]\n");
            break;
        } else {
            recv_buffer[bytes_read] = '\0';
            printf("%s", recv_buffer);
            fflush(stdout);
        }

        if (_kbhit()) {
            if (fgets(send_buffer, sizeof(send_buffer), stdin)) {
                trim_newline(send_buffer);
                if (strlen(send_buffer) == 0) continue;

                strcat(send_buffer, "\n");
                int sent = send(sock, send_buffer, (int)strlen(send_buffer), 0);
                if (sent == SOCKET_ERROR) {
                    fprintf(stderr, "\n[Failed to send message]\n");
                    break;
                }

                if (strcmp(send_buffer, "/quit\n") == 0) {
                    printf("Leaving chat...\n");
                    break;
                }
            }
        }

        Sleep(10);
    }
#endif

    socket_close(sock);
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}