#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>
#include "msg_struct.h"

#define BUFFER_SIZE 1024
#define MAX_PAYLOAD_SIZE 1024

char nickname[NICK_LEN] = "";

void send_message(int sockfd, enum msg_type type, const char *infos, const char *payload, int pld_len) {
    struct message msg;
    
    memset(&msg, 0, sizeof(struct message)); 
    strncpy(msg.nick_sender, nickname, NICK_LEN);
    msg.type = type;
    msg.pld_len = pld_len;
    
    if (infos) {
        strncpy(msg.infos, infos, INFOS_LEN);
    } else {
        memset(msg.infos, 0, INFOS_LEN);
    }

    write(sockfd, &msg, sizeof(struct message));
    if (pld_len > 0 && payload) {
        write(sockfd, payload, pld_len);
    }
}

void read_input(int sockfd, char *input) {
    if (strncmp(input, "/nick ", 6) == 0) {
        char *new_nick = input + 6;
        send_message(sockfd, NICKNAME_NEW, new_nick, NULL, 0);
    }
    else if (strcmp(input, "/who") == 0) {
        send_message(sockfd, NICKNAME_LIST, NULL, NULL, 0);
    }
    else if (strncmp(input, "/whois ", 7) == 0) {
        send_message(sockfd, NICKNAME_INFOS, input + 7, NULL, 0);
    }
    else if (strncmp(input, "/msgall ", 8) == 0) {
        send_message(sockfd, BROADCAST_SEND, NULL, input + 8, strlen(input + 8));
    }
    else if (strncmp(input, "/msg ", 5) == 0) {
        char *space = strchr(input + 5, ' ');
        if (!space) {
            printf("Usage: /msg <user> <message>\n");
            return;
        }
        *space = '\0';
        char *user = input + 5;
        char *message = space + 1;
        send_message(sockfd, UNICAST_SEND, user, message, strlen(message));
    }
    else if (strcmp(input, "/quit") == 0) {
        printf("Disconnected\n");
        exit(0);
    }
    else {
        send_message(sockfd, ECHO_SEND, NULL, input, strlen(input));
    }
}
void read_server_response(int sockfd) {
    struct message msg;
    memset(&msg, 0, sizeof(struct message)); 
    int bytes_read = read(sockfd, &msg, sizeof(struct message));
    if (bytes_read <= 0) {
        if (bytes_read == 0) {
            printf("Server is disconnected\n");
        } else {
            perror("read");
        }
        exit(1);
    }

    printf("Received message type: %d, pld_len: %d\n", msg.type, msg.pld_len);

    //verifier le pld-len
    if (msg.pld_len < 0 || msg.pld_len > MAX_PAYLOAD_SIZE ) {
        printf("Invalid payload : %d\n", msg.pld_len);
        return;
    }
    if (msg.pld_len > 0) {
        char payload[MAX_PAYLOAD_SIZE + 1] = {0};
        //Verifier la récéption du message
        int total_received = 0;
        while (total_received < msg.pld_len) {
            int to_read = msg.pld_len - total_received;
            if (to_read > MAX_PAYLOAD_SIZE - total_received) {
                to_read = MAX_PAYLOAD_SIZE - total_received;
            }
            
            int received = read(sockfd, payload + total_received, to_read);
            if (received <= 0) {
                perror("read payload");
                return;
            }
            total_received += received;
        }
        payload[msg.pld_len] = '\0';
        switch (msg.type) {
            case NICKNAME_NEW:
                printf("[Server] %s\n", payload);
                if (strstr(payload, "OK") != NULL) {
                    strncpy(nickname, msg.infos, NICK_LEN);
                }
                break;
            case NICKNAME_LIST:
                printf("[Users]\n%s\n", payload);
                break;
            case NICKNAME_INFOS:
                printf("[Info %s] %s\n", msg.infos, payload);
                break;
            case ECHO_SEND:
                printf("[Echo] %s\n", payload);
                break;
            case UNICAST_SEND:
                printf("[Private from %s] %s\n", msg.nick_sender, payload);
                break;
            case BROADCAST_SEND:
                printf("[%s] %s\n", msg.nick_sender, payload);
                break;
                default:
                printf("Unknown message type %d: %s\n", msg.type, payload); //Si un autre cas se présente
                break;
        }
    } else {
      
        switch (msg.type) {
            case NICKNAME_NEW:
                //onstocke le pseudo dans msg.infos
                if (strlen(msg.infos) > 0) {
                    strncpy(nickname, msg.infos, NICK_LEN);
                    printf("[Server] Nickname is changed to: %s\n", nickname); //on doit tenircompte du changement du pesudo d'un client
                }
                break;
                
            default:
                printf("Message type %d received (no payload)\n", msg.type);
                break;
        }
    }
}
//main
int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <IP> <PORT>\n", argv[0]);
        exit(1);
    }

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket");
        exit(1);
    }

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(atoi(argv[2]));
    inet_pton(AF_INET, argv[1], &serv_addr.sin_addr);

    if (connect(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("connect");
        exit(1);
    }

    printf("[Server] : please login with /nick <pseudo>\n");
    //intervention de poll
    struct pollfd sock_tab[2];
    sock_tab[0].fd = STDIN_FILENO;
    sock_tab[0].events = POLLIN;
    sock_tab[1].fd = sockfd;
    sock_tab[1].events = POLLIN;

    while (1) {
        if (poll(sock_tab, 2, -1) < 0) {
            perror("poll");
            break;
        }

        if (sock_tab[0].revents & POLLIN) {
            char input[BUFFER_SIZE];
            if (fgets(input, sizeof(input), stdin)) {
                input[strcspn(input, "\n")] = '\0';
                if (strlen(input) > 0) {
                    read_input(sockfd, input);
                }
            }
        }

        if (sock_tab[1].revents & POLLIN) {
            read_server_response(sockfd);
        }
    }
    close(sockfd);
    return 0;
}
