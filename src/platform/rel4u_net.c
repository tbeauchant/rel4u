#define _POSIX_C_SOURCE 200112L
#define _DEFAULT_SOURCE
#include "rel4u_net.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef NI_MAXHOST
#define NI_MAXHOST 1025
#endif
#ifndef NI_MAXSERV
#define NI_MAXSERV 32
#endif

#if defined(_WIN32)

static int g_net_initialized = 0;

int rel4u_net_init(void) {
    if (g_net_initialized == 0) {
        WSADATA wsa_data;
        int res = WSAStartup(MAKEWORD(2, 2), &wsa_data);
        if (res != 0) return -1;
        g_net_initialized = 1;
    }
    return 0;
}

void rel4u_net_cleanup(void) {
    if (g_net_initialized) {
        WSACleanup();
        g_net_initialized = 0;
    }
}

int rel4u_net_socket_set_nonblocking(rel4u_socket_t sock, bool nonblocking) {
    u_long mode = nonblocking ? 1 : 0;
    return ioctlsocket(sock, FIONBIO, &mode);
}

void rel4u_net_socket_close(rel4u_socket_t sock) {
    if (sock != REL4U_INVALID_SOCKET) {
        closesocket(sock);
    }
}

int rel4u_wakeup_pipe_create(rel4u_wakeup_pipe_t* pipe_obj) {
    rel4u_net_init();
    SOCKET listener = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (listener == INVALID_SOCKET) return -1;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;

    if (bind(listener, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
        closesocket(listener);
        return -1;
    }

    int addr_len = sizeof(addr);
    if (getsockname(listener, (struct sockaddr*)&addr, &addr_len) != 0) {
        closesocket(listener);
        return -1;
    }

    SOCKET sender = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sender == INVALID_SOCKET) {
        closesocket(listener);
        return -1;
    }

    if (connect(sender, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
        closesocket(sender);
        closesocket(listener);
        return -1;
    }

    rel4u_net_socket_set_nonblocking(listener, true);
    rel4u_net_socket_set_nonblocking(sender, true);

    pipe_obj->recv_sock = listener;
    pipe_obj->send_sock = sender;
    return 0;
}

void rel4u_wakeup_pipe_signal(rel4u_wakeup_pipe_t* pipe_obj) {
    if (pipe_obj->send_sock != REL4U_INVALID_SOCKET) {
        char byte = 1;
        send(pipe_obj->send_sock, &byte, 1, 0);
    }
}

void rel4u_wakeup_pipe_drain(rel4u_wakeup_pipe_t* pipe_obj) {
    if (pipe_obj->recv_sock != REL4U_INVALID_SOCKET) {
        char buf[64];
        while (recv(pipe_obj->recv_sock, buf, sizeof(buf), 0) > 0) {}
    }
}

void rel4u_wakeup_pipe_close(rel4u_wakeup_pipe_t* pipe_obj) {
    if (pipe_obj->send_sock != REL4U_INVALID_SOCKET) {
        closesocket(pipe_obj->send_sock);
        pipe_obj->send_sock = REL4U_INVALID_SOCKET;
    }
    if (pipe_obj->recv_sock != REL4U_INVALID_SOCKET) {
        closesocket(pipe_obj->recv_sock);
        pipe_obj->recv_sock = REL4U_INVALID_SOCKET;
    }
}

int rel4u_net_poll(rel4u_socket_t sock, const rel4u_wakeup_pipe_t* pipe_obj, int timeout_ms, bool* out_socket_readable, bool* out_pipe_readable) {
    WSAPOLLFD fds[2];
    int nfds = 0;

    if (out_socket_readable) *out_socket_readable = false;
    if (out_pipe_readable) *out_pipe_readable = false;

    if (sock != REL4U_INVALID_SOCKET) {
        fds[nfds].fd = sock;
        fds[nfds].events = POLLIN;
        fds[nfds].revents = 0;
        nfds++;
    }
    if (pipe_obj && pipe_obj->recv_sock != REL4U_INVALID_SOCKET) {
        fds[nfds].fd = pipe_obj->recv_sock;
        fds[nfds].events = POLLIN;
        fds[nfds].revents = 0;
        nfds++;
    }

    if (nfds == 0) return 0;

    int ret = WSAPoll(fds, nfds, timeout_ms);
    if (ret > 0) {
        int idx = 0;
        if (sock != REL4U_INVALID_SOCKET) {
            if (fds[idx].revents & POLLIN) {
                if (out_socket_readable) *out_socket_readable = true;
            }
            idx++;
        }
        if (pipe_obj && pipe_obj->recv_sock != REL4U_INVALID_SOCKET) {
            if (fds[idx].revents & POLLIN) {
                if (out_pipe_readable) *out_pipe_readable = true;
            }
            idx++;
        }
    }
    return ret;
}

#else /* POSIX / BSD / Linux */

int rel4u_net_init(void) {
    return 0;
}

void rel4u_net_cleanup(void) {
}

int rel4u_net_socket_set_nonblocking(rel4u_socket_t sock, bool nonblocking) {
    int flags = fcntl(sock, F_GETFL, 0);
    if (flags < 0) return -1;
    if (nonblocking) {
        flags |= O_NONBLOCK;
    } else {
        flags &= ~O_NONBLOCK;
    }
    return fcntl(sock, F_SETFL, flags);
}

void rel4u_net_socket_close(rel4u_socket_t sock) {
    if (sock != REL4U_INVALID_SOCKET) {
        close(sock);
    }
}

int rel4u_wakeup_pipe_create(rel4u_wakeup_pipe_t* pipe_obj) {
    int fds[2];
    if (pipe(fds) != 0) return -1;

    rel4u_net_socket_set_nonblocking(fds[0], true);
    rel4u_net_socket_set_nonblocking(fds[1], true);

    pipe_obj->read_fd = fds[0];
    pipe_obj->write_fd = fds[1];
    return 0;
}

void rel4u_wakeup_pipe_signal(rel4u_wakeup_pipe_t* pipe_obj) {
    if (pipe_obj->write_fd != -1) {
        char byte = 1;
        ssize_t written = write(pipe_obj->write_fd, &byte, 1);
        (void)written;
    }
}

void rel4u_wakeup_pipe_drain(rel4u_wakeup_pipe_t* pipe_obj) {
    if (pipe_obj->read_fd != -1) {
        char buf[64];
        while (read(pipe_obj->read_fd, buf, sizeof(buf)) > 0) {}
    }
}

void rel4u_wakeup_pipe_close(rel4u_wakeup_pipe_t* pipe_obj) {
    if (pipe_obj->write_fd != -1) {
        close(pipe_obj->write_fd);
        pipe_obj->write_fd = -1;
    }
    if (pipe_obj->read_fd != -1) {
        close(pipe_obj->read_fd);
        pipe_obj->read_fd = -1;
    }
}

int rel4u_net_poll(rel4u_socket_t sock, const rel4u_wakeup_pipe_t* pipe_obj, int timeout_ms, bool* out_socket_readable, bool* out_pipe_readable) {
    struct pollfd fds[2];
    int nfds = 0;

    if (out_socket_readable) *out_socket_readable = false;
    if (out_pipe_readable) *out_pipe_readable = false;

    if (sock != REL4U_INVALID_SOCKET) {
        fds[nfds].fd = sock;
        fds[nfds].events = POLLIN;
        fds[nfds].revents = 0;
        nfds++;
    }
    if (pipe_obj && pipe_obj->read_fd != -1) {
        fds[nfds].fd = pipe_obj->read_fd;
        fds[nfds].events = POLLIN;
        fds[nfds].revents = 0;
        nfds++;
    }

    if (nfds == 0) return 0;

    int ret = poll(fds, nfds, timeout_ms);
    if (ret > 0) {
        int idx = 0;
        if (sock != REL4U_INVALID_SOCKET) {
            if (fds[idx].revents & POLLIN) {
                if (out_socket_readable) *out_socket_readable = true;
            }
            idx++;
        }
        if (pipe_obj && pipe_obj->read_fd != -1) {
            if (fds[idx].revents & POLLIN) {
                if (out_pipe_readable) *out_pipe_readable = true;
            }
            idx++;
        }
    }
    return ret;
}

#endif

/* Common Socket & Address Functions */

int rel4u_net_addr_from_string(rel4u_net_addr_t* out_addr, const char* host, uint16_t port) {
    if (!out_addr) return -1;
    rel4u_net_init();
    memset(out_addr, 0, sizeof(*out_addr));

    struct addrinfo hints;
    struct addrinfo* res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_PASSIVE;

    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%u", (unsigned int)port);

    int err = getaddrinfo(host, port_str, &hints, &res);
    if (err != 0 || !res) {
        if (res) freeaddrinfo(res);
        return -1;
    }

    memcpy(&out_addr->addr, res->ai_addr, res->ai_addrlen);
    out_addr->len = (socklen_t)res->ai_addrlen;

    freeaddrinfo(res);
    return 0;
}

int rel4u_net_addr_to_string(const rel4u_net_addr_t* addr, char* out_str, size_t str_len, uint16_t* out_port) {
    if (!addr || !out_str || str_len == 0) return -1;
    rel4u_net_init();

    char host_buf[NI_MAXHOST];
    char serv_buf[NI_MAXSERV];

    int err = getnameinfo((const struct sockaddr*)&addr->addr, addr->len,
                          host_buf, sizeof(host_buf),
                          serv_buf, sizeof(serv_buf),
                          NI_NUMERICHOST | NI_NUMERICSERV);
    if (err != 0) return -1;

    snprintf(out_str, str_len, "%s", host_buf);
    if (out_port) {
        *out_port = (uint16_t)atoi(serv_buf);
    }
    return 0;
}

bool rel4u_net_addr_equal(const rel4u_net_addr_t* a, const rel4u_net_addr_t* b) {
    if (!a || !b) return false;
    if (a->addr.ss_family != b->addr.ss_family) return false;

    if (a->addr.ss_family == AF_INET) {
        const struct sockaddr_in* sa = (const struct sockaddr_in*)&a->addr;
        const struct sockaddr_in* sb = (const struct sockaddr_in*)&b->addr;
        return (sa->sin_port == sb->sin_port) &&
               (sa->sin_addr.s_addr == sb->sin_addr.s_addr);
    } else if (a->addr.ss_family == AF_INET6) {
        const struct sockaddr_in6* sa = (const struct sockaddr_in6*)&a->addr;
        const struct sockaddr_in6* sb = (const struct sockaddr_in6*)&b->addr;
        return (sa->sin6_port == sb->sin6_port) &&
               (memcmp(&sa->sin6_addr, &sb->sin6_addr, sizeof(struct in6_addr)) == 0);
    }
    return false;
}

rel4u_socket_t rel4u_net_socket_create_udp(bool ipv6) {
    rel4u_net_init();
    int family = ipv6 ? AF_INET6 : AF_INET;
    rel4u_socket_t sock = socket(family, SOCK_DGRAM, IPPROTO_UDP);
    if (sock != REL4U_INVALID_SOCKET) {
        rel4u_net_socket_set_nonblocking(sock, true);
    }
    return sock;
}

int rel4u_net_socket_bind(rel4u_socket_t sock, const rel4u_net_addr_t* addr) {
    if (sock == REL4U_INVALID_SOCKET || !addr) return -1;
    return bind(sock, (const struct sockaddr*)&addr->addr, addr->len);
}

int rel4u_net_socket_set_reuseaddr(rel4u_socket_t sock, bool reuse) {
    int val = reuse ? 1 : 0;
    return setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&val, sizeof(val));
}

int rel4u_net_socket_set_buffer_sizes(rel4u_socket_t sock, int send_size, int recv_size) {
    if (send_size > 0) {
        setsockopt(sock, SOL_SOCKET, SO_SNDBUF, (const char*)&send_size, sizeof(send_size));
    }
    if (recv_size > 0) {
        setsockopt(sock, SOL_SOCKET, SO_RCVBUF, (const char*)&recv_size, sizeof(recv_size));
    }
    return 0;
}

int rel4u_net_sendto(rel4u_socket_t sock, const void* buf, size_t len, const rel4u_net_addr_t* dest_addr) {
    if (sock == REL4U_INVALID_SOCKET || !buf || !dest_addr) return -1;
    return (int)sendto(sock, (const char*)buf, (int)len, 0,
                       (const struct sockaddr*)&dest_addr->addr, dest_addr->len);
}

int rel4u_net_recvfrom(rel4u_socket_t sock, void* buf, size_t max_len, rel4u_net_addr_t* src_addr) {
    if (sock == REL4U_INVALID_SOCKET || !buf) return -1;
    socklen_t addr_len = sizeof(src_addr->addr);
    int res = (int)recvfrom(sock, (char*)buf, (int)max_len, 0,
                            src_addr ? (struct sockaddr*)&src_addr->addr : NULL,
                            src_addr ? &addr_len : NULL);
    if (res >= 0 && src_addr) {
        src_addr->len = addr_len;
    }
    return res;
}
