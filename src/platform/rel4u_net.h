#ifndef REL4U_NET_H
#define REL4U_NET_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET rel4u_socket_t;
#define REL4U_INVALID_SOCKET INVALID_SOCKET
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <poll.h>
#include <unistd.h>
#include <fcntl.h>
typedef int rel4u_socket_t;
#define REL4U_INVALID_SOCKET (-1)
#endif

typedef struct rel4u_net_addr {
    struct sockaddr_storage addr;
    socklen_t               len;
} rel4u_net_addr_t;

typedef struct rel4u_wakeup_pipe {
#if defined(_WIN32)
    rel4u_socket_t send_sock;
    rel4u_socket_t recv_sock;
#else
    int read_fd;
    int write_fd;
#endif
} rel4u_wakeup_pipe_t;

/* System initialization (WSAStartup on Windows) */
int  rel4u_net_init(void);
void rel4u_net_cleanup(void);

/* Address helper functions */
int  rel4u_net_addr_from_string(rel4u_net_addr_t* out_addr, const char* host, uint16_t port);
int  rel4u_net_addr_to_string(const rel4u_net_addr_t* addr, char* out_str, size_t str_len, uint16_t* out_port);
bool rel4u_net_addr_equal(const rel4u_net_addr_t* a, const rel4u_net_addr_t* b);

/* Socket operations */
rel4u_socket_t rel4u_net_socket_create_udp(bool ipv6);
int  rel4u_net_socket_bind(rel4u_socket_t sock, const rel4u_net_addr_t* addr);
int  rel4u_net_socket_set_nonblocking(rel4u_socket_t sock, bool nonblocking);
int  rel4u_net_socket_set_reuseaddr(rel4u_socket_t sock, bool reuse);
int  rel4u_net_socket_set_buffer_sizes(rel4u_socket_t sock, int send_size, int recv_size);
void rel4u_net_socket_close(rel4u_socket_t sock);

int  rel4u_net_sendto(rel4u_socket_t sock, const void* buf, size_t len, const rel4u_net_addr_t* dest_addr);
int  rel4u_net_recvfrom(rel4u_socket_t sock, void* buf, size_t max_len, rel4u_net_addr_t* src_addr);

/* Wakeup channel for background worker polling */
int  rel4u_wakeup_pipe_create(rel4u_wakeup_pipe_t* pipe);
void rel4u_wakeup_pipe_signal(rel4u_wakeup_pipe_t* pipe);
void rel4u_wakeup_pipe_drain(rel4u_wakeup_pipe_t* pipe);
void rel4u_wakeup_pipe_close(rel4u_wakeup_pipe_t* pipe);

/* Polling helper */
int  rel4u_net_poll(rel4u_socket_t sock, const rel4u_wakeup_pipe_t* pipe, int timeout_ms, bool* out_socket_readable, bool* out_pipe_readable);

#endif /* REL4U_NET_H */
