#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "common.h"
int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <server_name> <server_port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    char *server_name = argv[1];
    int port = atoi(argv[2]);

    struct hostent *server = gethostbyname(server_name);
    if (!server) {
        fprintf(stderr, "Erreur: serveur introuvable\n");
        exit(EXIT_FAILURE);
    }

    int sockfd = socket(AF_INET, SOCK_STREAM, 0); //Création de la socket TCP IPv4
    if (sockfd < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(port);
    memcpy(&servaddr.sin_addr.s_addr, server->h_addr, server->h_length);

    if (connect(sockfd, (struct sockaddr*)&servaddr, sizeof(servaddr)) < 0) {
        perror("connection non établie");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    printf("Connection établie %s:%d\n", server_name, port);

    struct pollfd sock_tab[2];
    sock_tab[0].fd = STDIN_FILENO; //Entree clavier
    sock_tab[0].events = POLLIN;
    sock_tab[1].fd = sockfd;       //Socket serveur
    sock_tab[1].events = POLLIN;

    while (1) {
        int ret = poll(sock_tab, 2, -1);
        if (ret<0) {
            perror("poll");
            break;
        }

        //Lecture clavier
        if (sock_tab[0].revents & POLLIN) {
            char buf[1024];
            if (fgets(buf, sizeof(buf), stdin) == NULL) break;

            int msg_size = strlen(buf);
            //Envoyer la taille du message
            if (write(sockfd, &msg_size, sizeof(int)) <= 0) {
                perror("Erreur lors de l'envoi de la taille du message");
                close(sockfd);
                exit(EXIT_FAILURE);
            }
            //Envoyer le message
            if (write(sockfd, buf, msg_size) <= 0) {
                perror("Erreur lors de l'envoi du message");
                close(sockfd);
                exit(EXIT_FAILURE);
            }

            if (strncmp(buf, "/quit", 5) == 0) {
                printf("Disconnected\n"); //Message de déconnexion
                break;
            }
        }

        //Message serveur
        if (sock_tab[1].revents & POLLIN) {
            //Lire la taille du message
            int msg_size = 0;
            int ret = read(sockfd, &msg_size, sizeof(int));
            if (ret <= 0) {
                perror("Erreur lors de la réception de la taille du message");
                break;
            }
            //Vérifier sila taille est valide
            if (msg_size <= 0 || msg_size > MSG_LEN) {
                fprintf(stderr, "Erreur: taille du message invalide (%d)\n", msg_size);
                break;
            }
            //Lire le contenu du message
            char buf[MSG_LEN];
            int received = 0;
            memset(buf, 0, sizeof(buf));
            while (received < msg_size) {
                ret = read(sockfd, buf + received, msg_size - received);
                if (ret <= 0) {
                    perror("Erreur lors de la réception du message");
                    break;
                }
                received += ret;
            }
            buf[msg_size] ='\0'; 
            printf("Réponse serveur: %s", buf);
        }
    }
    close(sockfd);
    return 0;
}