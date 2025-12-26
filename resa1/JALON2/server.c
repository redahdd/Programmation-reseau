#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <poll.h>
#include <time.h>
#include <ctype.h>
#include "msg_struct.h"

typedef struct client {
    int fd;
    struct sockaddr_in addr;
    char nickname[NICK_LEN];
    time_t connection_time;
    struct client *next;
} connection;
connection *clients = NULL;
void add_client(int fd, struct sockaddr_in addr) {
    connection *new_client = malloc(sizeof(connection));
    if (!new_client) {
        perror("malloc");
        return;
    }
    new_client->fd = fd;
    new_client->addr = addr;
    new_client->nickname[0] = '\0';
    new_client->connection_time = time(NULL); //ajout du temps pour l'afficher en cas de la commande whois
    new_client->next = clients;
    clients = new_client;
}
void delete_client(int fd) {
    connection **ptr = &clients;
    while (*ptr) {
        if ((*ptr)->fd == fd) {
            connection *to_free = *ptr;
            *ptr = (*ptr)->next;
            close(to_free->fd);
            free(to_free);
            return;
        }
        ptr = &((*ptr)->next);
    }
}
connection* find_client_using_nick(const char *nick) {
    connection *c = clients;
    while (c) {
        if (strcmp(c->nickname, nick) == 0) return c;
        c = c->next;
    }
    return NULL;
}
//s'assurer que le psseudo est valide
int valider_nickname(const char *nick) {
    if (!nick || strlen(nick) == 0 || strlen(nick) >= NICK_LEN)
        return 0;
    for (int i = 0; nick[i]; i++) {
        if (!isalnum(nick[i])) return 0; 
    }
    return 1;
}

void send_message(int fd, enum msg_type type, const char *infos, const char *payload) {
    struct message msg;
    memset(&msg, 0, sizeof(msg));

    msg.type = type;
    if (infos) strncpy(msg.infos, infos, INFOS_LEN - 1);
    msg.pld_len = payload ? strlen(payload) : 0;

    write(fd, &msg, sizeof(msg));
    if (msg.pld_len > 0) {
        write(fd, payload, msg.pld_len);
    }
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        return 1;
    }
    int port = atoi(argv[1]);
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("socket");
        return 1;
    }
    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in server_addr = {0};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if (bind(listen_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        close(listen_fd);
        return 1;
    }
    if (listen(listen_fd, 10) < 0) {
        perror("listen");
        close(listen_fd);
        return 1;
    }
    printf("Server listening on %d...\n", port);
    int nb_max_sockets= 100 ;
    struct pollfd sock_array[nb_max_sockets];
    for (int i = 0; i < nb_max_sockets; i++) {
        sock_array[i].fd = -1;
        sock_array[i].events = POLLIN;
    }
    sock_array[0].fd = listen_fd;

    while (1) {
        int ret = poll(sock_array, nb_max_sockets, -1);
        if (ret < 0) {
            perror("poll");
            break;
        }
        for (int i = 0; i < nb_max_sockets; i++) {
            if (sock_array[i].fd == -1) continue;
            if (sock_array[i].fd == listen_fd && (sock_array[i].revents & POLLIN)) {
                struct sockaddr_in cliaddr;
                socklen_t len = sizeof(cliaddr);
                int newfd = accept(listen_fd, (struct sockaddr *)&cliaddr, &len);
                if (newfd < 0) {
                    perror("accept");
                    continue;
                }
                printf("new client is connected: %s:%d (fd=%d)\n",
                       inet_ntoa(cliaddr.sin_addr), ntohs(cliaddr.sin_port), newfd);

                add_client(newfd, cliaddr);

                for (int j = 0; j < nb_max_sockets; j++) {
                    if (sock_array[j].fd == -1) {
                        sock_array[j].fd = newfd;
                        sock_array[j].events = POLLIN;
                        break;
                    }
                }
                continue;
            }

            if (sock_array[i].revents & POLLIN) {
                int fd = sock_array[i].fd;
                struct message msg;
                int r = read(fd, &msg, sizeof(msg));
                if (r <= 0) {
                    printf("Client fd=%d disconnected\n", fd);
                    delete_client(fd);
                    sock_array[i].fd = -1;
                    continue;
                }

                char payload[MSG_LEN + 1] = {0};
                if (msg.pld_len > 0) {
                    int recvd = read(fd, payload, msg.pld_len);
                    if (recvd <= 0) {
                        perror("read payload");
                        delete_client(fd);
                        sock_array[i].fd = -1;
                        continue;
                    }
                    payload[msg.pld_len] = '\0';
                }
                connection *cli = clients;
                while (cli && cli->fd != fd) cli = cli->next;
                if (!cli) continue;
                switch (msg.type) {
                    case NICKNAME_NEW:
                        if (!valider_nickname(msg.infos)) {
                            send_message(fd, NICKNAME_NEW, "", "[Server] : Invalide nickname");
                        } else if (find_client_using_nick(msg.infos)) {
                            send_message(fd, NICKNAME_NEW, "", "[Server] : Nickname taken");
                        } else {
                            strncpy(cli->nickname, msg.infos, NICK_LEN - 1);
                            send_message(fd, NICKNAME_NEW, cli->nickname, " Nickname is valid");
                        }
                        break;

                    case NICKNAME_LIST: {
                        char list[MSG_LEN] = "[Server] : Online users are\n"; //voir tous les client connectes
                        connection *c = clients;
                        while (c) {
                            if (strlen(c->nickname) > 0) {
                                strcat(list, " - ");
                                strcat(list, c->nickname);
                                strcat(list, "\n");
                            }
                            c = c->next;
                        }
                        send_message(fd, NICKNAME_LIST, "", list);
                        break;
                    }

                    case NICKNAME_INFOS: {
                        connection *target = find_client_using_nick(msg.infos);
                        if (target) {
                            char info[MSG_LEN];
                            struct tm *tm_info = localtime(&target->connection_time);
                            char datebuf[64];
                            strftime(datebuf, sizeof(datebuf), "%Y/%m/%d@%H:%M", tm_info);
                            snprintf(info, sizeof(info),
                                "[Server] : %s connected since %s with IP %s and port %d",
                                target->nickname,
                                datebuf,
                                inet_ntoa(target->addr.sin_addr),
                                ntohs(target->addr.sin_port));
                            send_message(fd, NICKNAME_INFOS, target->nickname, info);
                        } else {
                            char err[128];
                            snprintf(err, sizeof(err), "[Server] : user %s does not exist", msg.infos);
                            send_message(fd, NICKNAME_INFOS, msg.infos, err);
                        }
                        break;
                    }

                    case BROADCAST_SEND: {
                        connection *c = clients;
                        while (c) {
                            if (c->fd != fd) {
                                send_message(c->fd, BROADCAST_SEND, cli->nickname, payload);
                            }
                            c = c->next;
                        }
                        break;
                    }

                    case UNICAST_SEND: {
                        connection *dest = find_client_using_nick(msg.infos);
                        if (dest) {
                            send_message(dest->fd, UNICAST_SEND, cli->nickname, payload);
                        } else {
                            char error_msg[256];
                            snprintf(error_msg, sizeof(error_msg),
                                     "[Server] : user %s does not exist", msg.infos);
                            send_message(fd, NICKNAME_INFOS, msg.infos, error_msg);
                        }
                        break;
                    }

                    case ECHO_SEND:
                        send_message(fd, ECHO_SEND, cli->nickname, payload);
                        break;

                    default:
                        send_message(fd, ECHO_SEND, "", "[Server] : invalid command");
                        break;
                }
            }
        }
    }

    close(listen_fd);
    return 0;
}
