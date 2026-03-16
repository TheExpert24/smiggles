#include "kernel.h"

#define SOCK_MAX 8
#define SOCK_EPHEMERAL_BASE 49152

typedef struct {
    int in_use;
    int type;
    uint16_t local_port;
} SockEntry;

static SockEntry sock_table[SOCK_MAX];
static uint16_t sock_next_ephemeral = SOCK_EPHEMERAL_BASE;

static int sock_valid_fd(int fd) {
    return fd >= 0 && fd < SOCK_MAX && sock_table[fd].in_use;
}

static uint16_t sock_allocate_ephemeral_port(void) {
    for (int attempts = 0; attempts < 16384; attempts++) {
        uint16_t candidate = sock_next_ephemeral;
        int in_use = 0;

        sock_next_ephemeral++;
        if (sock_next_ephemeral < SOCK_EPHEMERAL_BASE || sock_next_ephemeral == 0) {
            sock_next_ephemeral = SOCK_EPHEMERAL_BASE;
        }

        for (int i = 0; i < SOCK_MAX; i++) {
            if (!sock_table[i].in_use) continue;
            if (sock_table[i].local_port == candidate) {
                in_use = 1;
                break;
            }
        }

        if (!in_use) return candidate;
    }

    return SOCK_EPHEMERAL_BASE;
}

int sock_open_udp(void) {
    for (int i = 0; i < SOCK_MAX; i++) {
        if (!sock_table[i].in_use) {
            sock_table[i].in_use = 1;
            sock_table[i].type = SOCK_TYPE_UDP;
            sock_table[i].local_port = sock_allocate_ephemeral_port();
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

    // Poll the NIC with a busy-wait delay between each attempt.
    // net_poll_once() returns 0 immediately when the RX buffer is empty, so
    // without a delay the loop burns through in microseconds before QEMU's
    // virtual network stack has time to deliver the UDP reply (~1-5 ms round trip).
    r = udp_recv_next_for_port(sock_table[fd].local_port, src_ip_out, src_port_out, &dst_port, payload_out, max_payload, out_payload_len);
    if (r != 0) return r;

    for (int i = 0; i < 32768; i++) {
        net_poll_once();
        /* ~1 us busy delay per iteration, 32768 iters = ~32 ms max wait */
        for (volatile int d = 0; d < 500; d++) ;
        r = udp_recv_next_for_port(sock_table[fd].local_port, src_ip_out, src_port_out, &dst_port, payload_out, max_payload, out_payload_len);
        if (r != 0) return r;
    }

    return 0;
}

int sock_close(int fd) {
    if (!sock_valid_fd(fd)) return -1;
    udp_discard_for_port(sock_table[fd].local_port);
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
