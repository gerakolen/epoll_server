#include "utils.h"
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

typedef enum
{
    CHECK_ACK,
    MSG_WAIT,
    MSG
} WorkingOnState;

typedef struct
{
    WorkingOnState state;
    int send_pointer;
    int send_to_buf_end;
    uint8_t send_to_buf[1024];
} peer_position;

peer_position global_peer_state[16 * 1024];

typedef struct
{
    bool allow_read;
    bool allow_write;
} status_fds;

const status_fds WRITE_FD_STATUS = {.allow_read = false, .allow_write = true};

status_fds connected_peer(int socketFD, const struct sockaddr_in *peer_address, socklen_t peer_address_len)
{
    announce_connection(peer_address, peer_address_len);

    peer_position *peerpos = &global_peer_state[socketFD];
    peerpos->state = CHECK_ACK;
    peerpos->send_to_buf[0] = '/'; // Сообщение для проверки установления успешного соединения
    peerpos->send_pointer = 0;
    peerpos->send_to_buf_end = 1;
    return WRITE_FD_STATUS;
}

const status_fds READ_FD_STATUS = {.allow_read = true, .allow_write = false};
const status_fds NORW_FD_STATUS = {.allow_read = false, .allow_write = false};

status_fds peer_allow_recieve(int socketFD)
{
    peer_position *peerpos = &global_peer_state[socketFD];

    int sended_ptr = peerpos->send_pointer;
    int sended_buf = peerpos->send_to_buf_end;

    if (peerpos->state == CHECK_ACK || sended_ptr < sended_buf)
    {
        return WRITE_FD_STATUS;
    }

    uint8_t buffer[1024];
    int bytesRecieved = recv(socketFD, buffer, sizeof buffer, 0);

    if (bytesRecieved == 0)
    {
        return NORW_FD_STATUS;
    }
    else if (bytesRecieved < 0)
    {
        if (errno == EWOULDBLOCK || errno == EAGAIN)
        {
            return READ_FD_STATUS;
        }
        else
        {
            kill_peer("receive");
        }
    }

    bool ready_to_send = false;
    for (int i = 0; i < bytesRecieved; ++i)
    {
        WorkingOnState status = peerpos->state;
        switch (status)
        {
        case CHECK_ACK:
            break;
        case MSG_WAIT:
            if (buffer[i] == '^')
            {
                peerpos->state = MSG;
            }
            break;
        case MSG:
            if (buffer[i] == '$')
            {
                peerpos->state = MSG_WAIT;
            }
            else
            {
                peerpos->send_to_buf[peerpos->send_to_buf_end++] = buffer[i] + 1;
                ready_to_send = true;
            }
            break;
        }
    }
    return (status_fds){.allow_read = !ready_to_send, .allow_write = ready_to_send};
}

const status_fds READ_WRITE_FD_STATUS = {.allow_read = true, .allow_write = true};

status_fds peer_allow_send(int socketFD)
{
    peer_position *peerpos = &global_peer_state[socketFD];

    if (peerpos->send_pointer >= peerpos->send_to_buf_end)
    {
        return READ_WRITE_FD_STATUS;
    }

    int send_length = peerpos->send_to_buf_end - peerpos->send_pointer;
    int num_send = send(socketFD, &peerpos->send_to_buf[peerpos->send_pointer], send_length, 0);

    if (num_send == -1)
    {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
        {
            return WRITE_FD_STATUS;
        }
        else
        {
            kill_peer("send");
        }
    }

    if (num_send < send_length)
    {
        peerpos->send_pointer += num_send;
        return WRITE_FD_STATUS;
    }
    else
    {
        peerpos->send_pointer = 0;
        peerpos->send_to_buf_end = 0;

        if (peerpos->state == CHECK_ACK)
        {
            peerpos->state = MSG_WAIT;
        }

        return READ_FD_STATUS;
    }
}

int main(int argc, const char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);

    int port_num = 8080;
    if (argc >= 2)
    {
        port_num = atoi(argv[1]);
    }

    int listener = accept_socket(port_num);
    set_socket_unblock(listener);

    struct epoll_event get_event;
    get_event.data.fd = listener;
    get_event.events = EPOLLIN;

    int epoll_create_FD = epoll_create1(0);
    if (epoll_create_FD < 0)
    {
        kill_peer("epoll1");
    }

    struct epoll_event *events = calloc(16 * 1024, sizeof(struct epoll_event));

    if (events == NULL)
    {
        kill("");
    }

    if (epoll_ctl(epoll_create_FD, EPOLL_CTL_ADD, listener, &get_event) < 0)
    {
        kill_peer("");
    }

    while (true)
    {
        int ready = epoll_wait(epoll_create_FD, events, 16 * 1024, -1);
        for (int i = 0; i < ready; i++)
        {
            if (events[i].events & EPOLLERR)
            {
                kill_peer("EPOLLERR");
            }

            if (events[i].data.fd == listener)
            {
                struct sockaddr_in peer_address;
                socklen_t peer_address_len = sizeof(peer_address);
                int new_socket_FD = accept(listener, (struct sockaddr *)&peer_address, &peer_address_len);

                if (new_socket_FD < 0)
                {
                    kill_peer("accept");
                }
                else
                {
                    set_socket_unblock(new_socket_FD);
                    if (new_socket_FD >= 16 * 1024)
                    {
                        kill("", new_socket_FD, 16 * 1024);
                    }

                    status_fds status = connected_peer(new_socket_FD, &peer_address, peer_address_len);
                    struct epoll_event event = {0};
                    event.data.fd = new_socket_FD;

                    if (status.allow_read)
                    {
                        event.events |= EPOLLIN;
                    }
                    if (status.allow_write)
                    {
                        event.events |= EPOLLOUT;
                    }

                    if (epoll_ctl(epoll_create_FD, EPOLL_CTL_ADD, new_socket_FD, &event) < 0)
                    {
                        kill_peer("");
                    }
                }
            }
            else
            {
                if (events[i].events & EPOLLIN)
                {
                    int file_desc = events[i].data.fd;
                    status_fds status = peer_allow_recieve(file_desc);
                    struct epoll_event event = {0};
                    event.data.fd = file_desc;

                    if (status.allow_read)
                    {
                        event.events |= EPOLLIN;
                    }
                    if (status.allow_write)
                    {
                        event.events |= EPOLLOUT;
                    }
                    if (event.events == 0)
                    {
                        printf("закрытие сокета %d...\n", file_desc);
                        if (epoll_ctl(epoll_create_FD, EPOLL_CTL_DEL, file_desc, NULL) < 0)
                        {
                            kill_peer("");
                        }
                        close(file_desc);
                    }
                    else if (epoll_ctl(epoll_create_FD, EPOLL_CTL_MOD, file_desc, &event) < 0)
                    {
                        kill_peer("");
                    }
                }
                else if (events[i].events & EPOLLOUT)
                {
                    struct epoll_event event = {0};
                    int file_desc = events[i].data.fd;
                    status_fds status = peer_allow_send(file_desc);
                    event.data.fd = file_desc;

                    if (status.allow_read)
                    {
                        event.events |= EPOLLIN;
                    }
                    if (status.allow_write)
                    {
                        event.events |= EPOLLOUT;
                    }
                    if (event.events == 0)
                    {
                        printf("%d закрывается...\n", file_desc);
                        if (epoll_ctl(epoll_create_FD, EPOLL_CTL_DEL, file_desc, NULL) < 0)
                        {
                            kill_peer("");
                        }
                        close(file_desc);
                    }
                    else if (epoll_ctl(epoll_create_FD, EPOLL_CTL_MOD, file_desc, &event) < 0)
                    {
                        kill_peer("");
                    }
                }
            }
        }
    }

    return 0;
}