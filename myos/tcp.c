#include "kernel.h"

#define ETH_TYPE_IPV4_HI 0x08
#define ETH_TYPE_IPV4_LO 0x00

#define IP_PROTOCOL_TCP 6

#define TCP_FLAG_FIN 0x01
#define TCP_FLAG_SYN 0x02
#define TCP_FLAG_RST 0x04
#define TCP_FLAG_ACK 0x10

#define TCP_STATE_SYN_RCVD 1
#define TCP_STATE_ESTABLISHED 2

#define TCP_MAX_CONNS 8

typedef struct {
    uint8_t used;
    uint8_t state;
    uint8_t src_ip[4];
    uint8_t dst_ip[4];
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t snd_nxt;
    uint32_t rcv_nxt;
} TcpConn;

static TCPStats tcp_stats;
static TcpConn tcp_conns[TCP_MAX_CONNS];
static int tcp_listen_enabled = 0;
static uint16_t tcp_listen_port = 0;
static uint16_t tcp_next_ip_id = 1;
static uint32_t tcp_next_isn = 0x534D1000u;

static void copy_bytes(uint8_t* dst, const uint8_t* src, int len) {
    for (int i = 0; i < len; i++) dst[i] = src[i];
}

static int bytes_equal(const uint8_t* a, const uint8_t* b, int len) {
    for (int i = 0; i < len; i++) {
        if (a[i] != b[i]) return 0;
    }
    return 1;
}

static uint16_t read_be16(const uint8_t* p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static uint32_t read_be32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static void write_be16(uint8_t* p, uint16_t v) {
    p[0] = (uint8_t)((v >> 8) & 0xFF);
    p[1] = (uint8_t)(v & 0xFF);
}

static void write_be32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)((v >> 24) & 0xFF);
    p[1] = (uint8_t)((v >> 16) & 0xFF);
    p[2] = (uint8_t)((v >> 8) & 0xFF);
    p[3] = (uint8_t)(v & 0xFF);
}

static uint16_t internet_checksum(const uint8_t* data, int len) {
    uint32_t sum = 0;

    for (int i = 0; i + 1 < len; i += 2) {
        sum += (uint16_t)((data[i] << 8) | data[i + 1]);
        while (sum > 0xFFFFu) sum = (sum & 0xFFFFu) + (sum >> 16);
    }

    if (len & 1) {
        sum += (uint16_t)(data[len - 1] << 8);
        while (sum > 0xFFFFu) sum = (sum & 0xFFFFu) + (sum >> 16);
    }

    return (uint16_t)(~sum);
}

static uint16_t tcp_checksum(const uint8_t src_ip[4], const uint8_t dst_ip[4], const uint8_t* tcp_seg, int tcp_len) {
    uint32_t sum = 0;

    sum += ((uint16_t)src_ip[0] << 8) | src_ip[1];
    sum += ((uint16_t)src_ip[2] << 8) | src_ip[3];
    sum += ((uint16_t)dst_ip[0] << 8) | dst_ip[1];
    sum += ((uint16_t)dst_ip[2] << 8) | dst_ip[3];
    sum += (uint16_t)IP_PROTOCOL_TCP;
    sum += (uint16_t)tcp_len;
    while (sum > 0xFFFFu) sum = (sum & 0xFFFFu) + (sum >> 16);

    for (int i = 0; i + 1 < tcp_len; i += 2) {
        sum += (uint16_t)((tcp_seg[i] << 8) | tcp_seg[i + 1]);
        while (sum > 0xFFFFu) sum = (sum & 0xFFFFu) + (sum >> 16);
    }

    if (tcp_len & 1) {
        sum += (uint16_t)(tcp_seg[tcp_len - 1] << 8);
        while (sum > 0xFFFFu) sum = (sum & 0xFFFFu) + (sum >> 16);
    }

    return (uint16_t)(~sum);
}

static int tcp_find_conn(const uint8_t src_ip[4], const uint8_t dst_ip[4], uint16_t src_port, uint16_t dst_port) {
    for (int i = 0; i < TCP_MAX_CONNS; i++) {
        TcpConn* c = &tcp_conns[i];
        if (!c->used) continue;
        if (c->src_port != src_port || c->dst_port != dst_port) continue;
        if (!bytes_equal(c->src_ip, src_ip, 4)) continue;
        if (!bytes_equal(c->dst_ip, dst_ip, 4)) continue;
        return i;
    }
    return -1;
}

static int tcp_alloc_conn(void) {
    for (int i = 0; i < TCP_MAX_CONNS; i++) {
        if (!tcp_conns[i].used) return i;
    }

    // Replace oldest slot in this minimal implementation.
    return 0;
}

static int tcp_send_segment(
    const uint8_t dst_mac[6],
    const uint8_t src_ip[4],
    const uint8_t dst_ip[4],
    uint16_t src_port,
    uint16_t dst_port,
    uint32_t seq,
    uint32_t ack,
    uint8_t flags,
    uint16_t window,
    const uint8_t* payload,
    int payload_len
) {
    uint8_t frame[14 + 20 + 20 + 32];
    uint8_t* ip;
    uint8_t* tcp;
    int tcp_len;
    int ip_total_len;
    Rtl8139Status status;

    if (!dst_mac || !src_ip || !dst_ip) return 0;
    if (payload_len < 0 || payload_len > 32) return 0;
    if (!rtl8139_get_status(&status) || !status.initialized) return 0;

    copy_bytes(frame + 0, dst_mac, 6);
    copy_bytes(frame + 6, status.mac, 6);
    frame[12] = ETH_TYPE_IPV4_HI;
    frame[13] = ETH_TYPE_IPV4_LO;

    tcp_len = 20 + payload_len;
    ip_total_len = 20 + tcp_len;

    ip = frame + 14;
    ip[0] = 0x45;
    ip[1] = 0x00;
    write_be16(ip + 2, (uint16_t)ip_total_len);
    write_be16(ip + 4, tcp_next_ip_id++);
    write_be16(ip + 6, 0x0000);
    ip[8] = 64;
    ip[9] = IP_PROTOCOL_TCP;
    write_be16(ip + 10, 0x0000);
    copy_bytes(ip + 12, src_ip, 4);
    copy_bytes(ip + 16, dst_ip, 4);
    write_be16(ip + 10, internet_checksum(ip, 20));

    tcp = ip + 20;
    write_be16(tcp + 0, src_port);
    write_be16(tcp + 2, dst_port);
    write_be32(tcp + 4, seq);
    write_be32(tcp + 8, ack);
    tcp[12] = 0x50; // data offset 5, no options
    tcp[13] = flags;
    write_be16(tcp + 14, window);
    write_be16(tcp + 16, 0x0000);
    write_be16(tcp + 18, 0x0000);

    if (payload_len > 0 && payload) {
        copy_bytes(tcp + 20, payload, payload_len);
    }

    write_be16(tcp + 16, tcp_checksum(src_ip, dst_ip, tcp, tcp_len));

    return rtl8139_send_frame(frame, 14 + ip_total_len) > 0 ? 1 : 0;
}

int tcp_poll_once(void) {
    return net_poll_once();
}

int tcp_process_frame(const uint8_t* frame, int length) {
    int ip_available;

    if (!frame || length <= 0) return -10;

    tcp_stats.frames_polled++;

    if (length < 14 + 20 + 20) return 2;
    if (frame[12] != ETH_TYPE_IPV4_HI || frame[13] != ETH_TYPE_IPV4_LO) return 2;

    ip_available = length - 14;
    if (ip_available < (20 + 20)) return 2;

    {
        const uint8_t* ip = frame + 14;
        uint8_t version = (uint8_t)(ip[0] >> 4);
        uint8_t ihl_words = (uint8_t)(ip[0] & 0x0F);
        int ip_hlen = (int)ihl_words * 4;
        int total_len = (int)read_be16(ip + 2);

        if (version != 4 || ihl_words < 5 || ip_hlen > 60) {
            tcp_stats.parse_errors++;
            return -1;
        }

        if (total_len > ip_available) total_len = ip_available;
        if (total_len < (ip_hlen + 20)) {
            tcp_stats.parse_errors++;
            return -2;
        }

        if (ip[9] != IP_PROTOCOL_TCP) return 3;

        {
            const uint8_t* tcp = ip + ip_hlen;
            int tcp_available = total_len - ip_hlen;
            uint16_t src_port = read_be16(tcp + 0);
            uint16_t dst_port = read_be16(tcp + 2);
            uint32_t seq = read_be32(tcp + 4);
            uint32_t ack = read_be32(tcp + 8);
            int tcp_hlen = (int)((tcp[12] >> 4) & 0x0F) * 4;
            uint8_t flags = tcp[13];
            int payload_len;
            uint8_t local_ip[4];
            int conn_idx;

            if (tcp_hlen < 20 || tcp_hlen > tcp_available) {
                tcp_stats.parse_errors++;
                return -3;
            }

            payload_len = tcp_available - tcp_hlen;
            tcp_stats.tcp_seen++;

            if (flags & TCP_FLAG_RST) tcp_stats.rst_seen++;

            if (!arp_get_local_ip(local_ip)) return 3;
            if (!bytes_equal(ip + 16, local_ip, 4)) return 3;

            conn_idx = tcp_find_conn(ip + 12, ip + 16, src_port, dst_port);
            if (conn_idx >= 0) {
                TcpConn* c = &tcp_conns[conn_idx];

                if (c->state == TCP_STATE_SYN_RCVD) {
                    if ((flags & TCP_FLAG_ACK) && !(flags & TCP_FLAG_SYN) && ack == c->snd_nxt) {
                        c->state = TCP_STATE_ESTABLISHED;
                        tcp_stats.established++;
                        return 1;
                    }
                    if (flags & TCP_FLAG_RST) {
                        c->used = 0;
                        return 1;
                    }
                    return 1;
                }

                if (c->state == TCP_STATE_ESTABLISHED) {
                    uint32_t advance = (uint32_t)payload_len;
                    if (flags & TCP_FLAG_FIN) advance += 1u;
                    if (advance > 0u) {
                        c->rcv_nxt = seq + advance;
                        if (tcp_send_segment(frame + 6, ip + 16, ip + 12, dst_port, src_port, c->snd_nxt, c->rcv_nxt, TCP_FLAG_ACK, 64240, 0, 0)) {
                            tcp_stats.ack_sent++;
                        }
                        if (flags & TCP_FLAG_FIN) {
                            c->used = 0;
                        }
                    }
                    return 1;
                }

                return 1;
            }

            if (tcp_listen_enabled && dst_port == tcp_listen_port && (flags & TCP_FLAG_SYN) && !(flags & TCP_FLAG_ACK)) {
                TcpConn* c;
                uint32_t isn = tcp_next_isn;
                int slot = tcp_alloc_conn();
                c = &tcp_conns[slot];

                c->used = 1;
                c->state = TCP_STATE_SYN_RCVD;
                copy_bytes(c->src_ip, ip + 12, 4);
                copy_bytes(c->dst_ip, ip + 16, 4);
                c->src_port = src_port;
                c->dst_port = dst_port;
                c->rcv_nxt = seq + 1u;
                c->snd_nxt = isn + 1u;

                if (tcp_send_segment(frame + 6, ip + 16, ip + 12, dst_port, src_port, isn, c->rcv_nxt, (uint8_t)(TCP_FLAG_SYN | TCP_FLAG_ACK), 64240, 0, 0)) {
                    tcp_stats.syn_received++;
                    tcp_stats.synack_sent++;
                    tcp_next_isn += 0x101u;
                    return 1;
                }

                c->used = 0;
                return -4;
            }
        }
    }

    return 3;
}

int tcp_set_listen_port(uint16_t port) {
    if (port == 0) return 0;
    tcp_listen_enabled = 1;
    tcp_listen_port = port;
    return 1;
}

int tcp_clear_listen_port(void) {
    tcp_listen_enabled = 0;
    tcp_listen_port = 0;
    return 1;
}

int tcp_get_listen_port(uint16_t* out_port) {
    if (!out_port) return 0;
    *out_port = tcp_listen_port;
    return tcp_listen_enabled ? 1 : 0;
}

int tcp_get_stats(TCPStats* out_stats) {
    if (!out_stats) return 0;
    *out_stats = tcp_stats;
    return 1;
}

int tcp_get_conn_count(void) {
    int count = 0;
    for (int i = 0; i < TCP_MAX_CONNS; i++) {
        if (tcp_conns[i].used) count++;
    }
    return count;
}

int tcp_get_conn_info(int index, TCPConnInfo* out_info) {
    int seen = 0;

    if (index < 0 || !out_info) return 0;

    for (int i = 0; i < TCP_MAX_CONNS; i++) {
        TcpConn* c = &tcp_conns[i];
        if (!c->used) continue;
        if (seen == index) {
            copy_bytes(out_info->src_ip, c->src_ip, 4);
            copy_bytes(out_info->dst_ip, c->dst_ip, 4);
            out_info->src_port = c->src_port;
            out_info->dst_port = c->dst_port;
            out_info->state = c->state;
            return 1;
        }
        seen++;
    }

    return 0;
}
