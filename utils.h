#ifndef UTILS_H
#define UTILS_H

#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>

void kill(char *fmt, ...);

void kill_peer(char *msg);

int accept_socket(int portnum);

void set_socket_unblock(int sockfd);

void announce_connection(const struct sockaddr_in *sa, socklen_t salen);

#endif