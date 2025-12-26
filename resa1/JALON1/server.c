#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <poll.h>
#include "common.h"

typedef struct client {
    int fd;
    struct sockaddr_in addr;
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
    new_client->next = clients;
    clients = new_client;
}
void delet_client(int fd) {
    connection **ptr = &clients;
    while (*ptr) {
        if ((*ptr)->fd == fd) {
            connection *to_free = *ptr;
            *ptr = (*ptr)->next;
            close(to_free->fd);
            free(to_free);
            return;
        }
        ptr= &((*ptr)->next);
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
        perror("sockt");
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
   //intervention de "poll"
    int nbr_max_sockets= 100 ;

    struct pollfd sock_array[nbr_max_sockets];
    for (int i = 0; i < nbr_max_sockets; i++) {
        sock_array[i].fd = -1; 
        sock_array[i].events = POLLIN;
    }
    sock_array[0].fd = listen_fd; //socket d’ecoute 
    while (1) {
        int ret = poll(sock_array,nbr_max_sockets, -1);
        if (ret < 0) {
            perror("poll");
            break;
        }
           for (int i = 0; i < nbr_max_sockets; i++) {
            if (sock_array[i].fd == -1) continue; //verifie si la case est libre

             if (sock_array[i].fd == listen_fd && (sock_array[i].revents & POLLIN)) {
                struct sockaddr_in cliaddr;
                socklen_t len = sizeof(cliaddr);
                int newfd = accept(listen_fd, (struct sockaddr *)&cliaddr, &len);
                if (newfd < 0) {
                    perror("accept");
                    continue;
                }
                printf("new client conected: %s:%d (fd=%d)\n",
                       inet_ntoa(cliaddr.sin_addr), ntohs(cliaddr.sin_port), newfd);

                add_client(newfd, cliaddr);

                int j;
                for (j = 0; j < nbr_max_sockets; j++) {
                    if (sock_array[j].fd == -1) {
                        sock_array[j].fd = newfd;
                        sock_array[j].events = POLLIN;
                        break;
                    }
                }
                continue;
            }
                if (sock_array[i].revents & POLLIN) {
                int newfd = sock_array[i].fd;
                //faut recevoir le nombre d'octet 
                int msg_size = 0;
                int bytes_to_read = sizeof(int);
                int received = 0;
                int ret = 0;
                while (received < bytes_to_read) {
                    ret = read(newfd, (char *)&msg_size + received, bytes_to_read - received);
                    if (ret <= 0) {
                        perror("read");
                        delet_client(newfd);
                        sock_array[i].fd = -1;
                        break;
                    }
                    received += ret;
                }
                if (sock_array[i].fd == -1) continue; 
                //reception de la chaine de caractères 
                int to_recv = msg_size;
                received = 0;
                ret = 0;
                char buffer[MSG_LEN];
                memset(buffer, 0, sizeof(buffer));
                while (received < to_recv) {
                    ret = read(newfd, buffer + received, to_recv - received);
                    if (ret <= 0) {
                        perror("read");
                        delet_client(newfd);
                        sock_array[i].fd = -1;
                        break;
                    }
                    received += ret;
                }
                if (sock_array[i].fd == -1) continue; 
                buffer[to_recv] = '\0';
                printf("received from fd=%d → %s\n", newfd, buffer);
                //Si client envoie "/quit", on le déconnecte
                if (strncmp(buffer, "/quit", 5) == 0) {
                    printf("Client fd=%d is disconected.\n", newfd);
                    delet_client(newfd);
                    sock_array[i].fd = -1;
                    continue;
                }
                //Envoi de la taille du message
                msg_size = strlen(buffer);
                if (write(newfd, &msg_size, sizeof(int)) <= 0) {
                    perror("Error sending message size");
                    delet_client(newfd);
                    sock_array[i].fd = -1;
                    continue;
                }
                //Envoyer le contenu du message
                if (write(newfd, buffer, msg_size) <= 0) {
                    perror("Error sending message");
                    delet_client(newfd);
                    sock_array[i].fd = -1;
                    continue;
                }
            }
        }
    }
   close(listen_fd);
    return 0;
}