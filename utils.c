#include "utils.h"
#include <fcntl.h>
#include <netdb.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>

void kill(char *template, ...)
{
    va_list params;
    va_start(params, template);
    vfprintf(stderr, template, params);
    va_end(params);

    fprintf(stderr, "\n");

    exit(EXIT_FAILURE);
}

void announce_connection(const struct sockaddr_in *sa, socklen_t salen)
{
    char hostbuf[1025];
    char portbuf[32];
    if (getnameinfo((struct sockaddr *)sa, salen, hostbuf, 1025, portbuf, 32, 0) == 0)
    {
        printf("клиент (%s, %s) подключен\n", hostbuf, portbuf);
    }
}

void kill_peer(char *msg)
{
    perror(msg);
    exit(EXIT_FAILURE);
}

int accept_socket(int portnum)
{
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
    {
        kill_peer("не удалось открыть сокет");
    }

    int opt = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
    {
        kill_peer("");
    }

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(portnum);

    if (bind(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    {
        kill_peer("");
    }

    if (listen(sockfd, 64) < 0)
    {
        kill_peer("ошибка прослушивания порта");
    }

    return sockfd;
}

void set_socket_unblock(int sockfd)
{
    int flags = fcntl(sockfd, F_GETFL, 0);
    if (flags == -1)
    {
        kill_peer("");
    }

    if (fcntl(sockfd, F_SETFL, flags | O_NONBLOCK) == -1)
    {
        kill_peer("");
    }
}
