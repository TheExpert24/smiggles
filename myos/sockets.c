#include "kernel.h"

#define SOCK_MAX 8
#define SOCK_EPHEMERAL_BASE 49152

typedef struct {
    int in_use;
    int type;
    uint16_t local_port;
} SockEntry;

static SockEntry sock_table[SOCK_MAX];

static int sock_valid_fd(int fd) {
    return fd >= 0 && fd < SOCK_MAX && sock_table[fd].in_use;
}

int sock_open_udp(void) {
    for (int i = 0; i < SOCK_MAX; i++) {
        if (!sock_table[i].in_use) {
            sock_table[i].in_use = 1;
            sock_table[i].type = SOCK_TYPE_UDP;
            sock_table[i].local_port = (uint16_t)(SOCK_EPHEMERAL_BASE + i);
            return i;
        }
    }
    return -1;
}

int sock_bind(int fd, uint16_t local_port) {
    if (!sock_valid_fd(fd)) return -1;
    if (sock_table[fd].type != SOCK_TYPE_UDP) return -2;
    if (local_port == 0) return -3;

    for (int i = 0; i < SOCK_MAX; i++) {
        if (i == fd) continue;
        if (!sock_table[i].in_use) continue;
        if (sock_table[i].type != SOCK_TYPE_UDP) continue;
        if (sock_table[i].local_port == local_port) return -4;
    }

    sock_table[fd].local_port = local_port;
    return 1;
}

int sock_sendto(int fd, const uint8_t target_ip[4], uint16_t target_port, const uint8_t* payload, int payload_len) {
    if (!sock_valid_fd(fd)) return -1;
    if (sock_table[fd].type != SOCK_TYPE_UDP) return -2;
    if (target_port == 0) return -3;
    return udp_send_datagram(target_ip, sock_table[fd].local_port, target_port, payload, payload_len);
}

int sock_recvfrom(int fd, uint8_t src_ip_out[4], uint16_t* src_port_out, uint8_t* payload_out, int max_payload, int* out_payload_len) {
    uint16_t dst_port = 0;
    int r;

    if (!sock_valid_fd(fd)) return -1;
    if (sock_table[fd].type != SOCK_TYPE_UDP) return -2;

    // Try queue first, then poll several frames so ARP+UDP can resolve in one recv call.
    r = udp_recv_next_for_port(sock_table[fd].local_port, src_ip_out, src_port_out, &dst_port, payload_out, max_payload, out_payload_len);
    if (r != 0) return r;

    for (int i = 0; i < 6; i++) {
        int p = net_poll_once();
        r = udp_recv_next_for_port(sock_table[fd].local_port, src_ip_out, src_port_out, &dst_port, payload_out, max_payload, out_payload_len);
        if (r != 0) return r;
        if (p <= 0) break;
    }

    return 0;
}

int sock_close(int fd) {
    if (!sock_valid_fd(fd)) return -1;
    sock_table[fd].in_use = 0;
    sock_table[fd].type = 0;
    sock_table[fd].local_port = 0;
    return 1;
}

int sock_get_count(void) {
    int count = 0;
    for (int i = 0; i < SOCK_MAX; i++) {
        if (sock_table[i].in_use) count++;
    }
    return count;
}

int sock_get_info(int index, SocketInfo* out_info) {
    int seen = 0;

    if (index < 0 || !out_info) return 0;

    for (int i = 0; i < SOCK_MAX; i++) {
        if (!sock_table[i].in_use) continue;
        if (seen == index) {
            out_info->in_use = sock_table[i].in_use;
            out_info->type = sock_table[i].type;
            out_info->local_port = sock_table[i].local_port;
            return 1;
        }
        seen++;
    }

    return 0;
}
