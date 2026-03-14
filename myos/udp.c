#include "kernel.h"

#define ETH_TYPE_IPV4_HI 0x08
#define ETH_TYPE_IPV4_LO 0x00

#define IP_PROTOCOL_UDP 17

#define UDP_RX_QUEUE_SIZE 8
#define UDP_RX_MAX_PAYLOAD 512

typedef struct {
    uint8_t valid;
    uint8_t src_ip[4];
    uint16_t src_port;
    uint16_t dst_port;
    uint16_t payload_len;
    uint8_t payload[UDP_RX_MAX_PAYLOAD];
} UdpRxEntry;

static UDPStats udp_stats;
static uint16_t udp_next_ip_id = 1;
static int udp_listen_enabled = 0;
static uint16_t udp_listen_port = 0;

static UdpRxEntry udp_rx_queue[UDP_RX_QUEUE_SIZE];
static int udp_rx_head = 0;
static int udp_rx_tail = 0;
static int udp_rx_count = 0;

static void copy_bytes(uint8_t* dst, const uint8_t* src, int len) {
    for (int i = 0; i < len; i++) dst[i] = src[i];
}

static uint16_t read_be16(const uint8_t* p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static void write_be16(uint8_t* p, uint16_t v) {
    p[0] = (uint8_t)((v >> 8) & 0xFF);
    p[1] = (uint8_t)(v & 0xFF);
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

static int udp_queue_push(const uint8_t src_ip[4], uint16_t src_port, uint16_t dst_port, const uint8_t* payload, uint16_t payload_len) {
    UdpRxEntry* e;

    if (!src_ip || !payload) return 0;
    if (payload_len > UDP_RX_MAX_PAYLOAD) return 0;

    if (udp_rx_count == UDP_RX_QUEUE_SIZE) {
        udp_stats.recv_dropped++;
        return 0;
    }

    e = &udp_rx_queue[udp_rx_tail];
    e->valid = 1;
    copy_bytes(e->src_ip, src_ip, 4);
    e->src_port = src_port;
    e->dst_port = dst_port;
    e->payload_len = payload_len;
    copy_bytes(e->payload, payload, payload_len);

    udp_rx_tail = (udp_rx_tail + 1) % UDP_RX_QUEUE_SIZE;
    udp_rx_count++;
    udp_stats.recv_queued++;
    return 1;
}

int udp_send_datagram(const uint8_t target_ip[4], uint16_t src_port, uint16_t dst_port, const uint8_t* payload, int payload_len) {
    uint8_t frame[14 + 20 + 8 + UDP_RX_MAX_PAYLOAD];
    uint8_t* ip;
    uint8_t* udp;
    uint8_t local_ip[4];
    uint8_t dst_mac[6];
    Rtl8139Status status;
    int udp_len;
    int ip_total_len;

    if (!target_ip || !payload || payload_len < 0 || payload_len > UDP_RX_MAX_PAYLOAD) return -1;
    if (!rtl8139_get_status(&status) || !status.initialized) return -2;
    if (!arp_get_local_ip(local_ip)) return -3;
    if (!arp_lookup_mac(target_ip, dst_mac)) return -4;

    udp_len = 8 + payload_len;
    ip_total_len = 20 + udp_len;

    copy_bytes(frame + 0, dst_mac, 6);
    copy_bytes(frame + 6, status.mac, 6);
    frame[12] = ETH_TYPE_IPV4_HI;
    frame[13] = ETH_TYPE_IPV4_LO;

    ip = frame + 14;
    ip[0] = 0x45;
    ip[1] = 0x00;
    write_be16(ip + 2, (uint16_t)ip_total_len);
    write_be16(ip + 4, udp_next_ip_id++);
    write_be16(ip + 6, 0x0000);
    ip[8] = 64;
    ip[9] = IP_PROTOCOL_UDP;
    write_be16(ip + 10, 0x0000);
    copy_bytes(ip + 12, local_ip, 4);
    copy_bytes(ip + 16, target_ip, 4);
    write_be16(ip + 10, internet_checksum(ip, 20));

    udp = ip + 20;
    write_be16(udp + 0, src_port);
    write_be16(udp + 2, dst_port);
    write_be16(udp + 4, (uint16_t)udp_len);
    // IPv4 allows UDP checksum 0, which keeps this first implementation simple.
    write_be16(udp + 6, 0x0000);

    if (payload_len > 0) {
        copy_bytes(udp + 8, payload, payload_len);
    }

    if (rtl8139_send_frame(frame, 14 + ip_total_len) > 0) {
        udp_stats.sent_packets++;
        return 1;
    }

    return -5;
}

int udp_poll_once(void) {
    return net_poll_once();
}

int udp_process_frame(const uint8_t* frame, int length) {
    int ip_available;

    if (!frame || length <= 0) return -10;

    udp_stats.frames_polled++;

    if (length < 14 + 20 + 8) return 2;
    if (frame[12] != ETH_TYPE_IPV4_HI || frame[13] != ETH_TYPE_IPV4_LO) return 2;

    ip_available = length - 14;
    if (ip_available < (20 + 8)) return 2;

    {
        const uint8_t* ip = frame + 14;
        uint8_t version = (uint8_t)(ip[0] >> 4);
        uint8_t ihl_words = (uint8_t)(ip[0] & 0x0F);
        int ip_hlen = (int)ihl_words * 4;
        int total_len = (int)read_be16(ip + 2);

        if (version != 4 || ihl_words < 5 || ip_hlen > 60) {
            udp_stats.parse_errors++;
            return -1;
        }

        if (total_len > ip_available) {
            total_len = ip_available;
        }

        if (total_len < (ip_hlen + 8)) {
            udp_stats.parse_errors++;
            return -2;
        }

        if (ip[9] != IP_PROTOCOL_UDP) {
            udp_stats.non_udp_ipv4++;
            return 3;
        }

        {
            const uint8_t* udp = ip + ip_hlen;
            uint16_t src_port = read_be16(udp + 0);
            uint16_t dst_port = read_be16(udp + 2);
            int udp_len = (int)read_be16(udp + 4);
            int payload_len;
            int udp_available = total_len - ip_hlen;

            if (udp_len < 8) {
                udp_stats.parse_errors++;
                return -3;
            }

            if (udp_len > udp_available) {
                udp_len = udp_available;
            }

            payload_len = udp_len - 8;

            udp_stats.udp_seen++;
            copy_bytes(udp_stats.last_src_ip, ip + 12, 4);
            copy_bytes(udp_stats.last_dst_ip, ip + 16, 4);
            udp_stats.last_src_port = src_port;
            udp_stats.last_dst_port = dst_port;
            udp_stats.last_payload_length = (uint16_t)payload_len;

            if (payload_len > UDP_RX_MAX_PAYLOAD) {
                udp_stats.recv_dropped++;
                return 1;
            }

            udp_queue_push(ip + 12, src_port, dst_port, udp + 8, (uint16_t)payload_len);
            return 1;
        }
    }
}

int udp_recv_next(uint8_t src_ip_out[4], uint16_t* src_port_out, uint16_t* dst_port_out, uint8_t* payload_out, int max_payload, int* out_payload_len) {
    UdpRxEntry* e;

    if (!src_ip_out || !src_port_out || !dst_port_out || !payload_out || !out_payload_len) return 0;
    if (max_payload <= 0) return 0;
    if (udp_listen_enabled) {
        return udp_recv_next_for_port(udp_listen_port, src_ip_out, src_port_out, dst_port_out, payload_out, max_payload, out_payload_len);
    }
    if (udp_rx_count == 0) return 0;

    e = &udp_rx_queue[udp_rx_head];
    if (!e->valid) return 0;
    if (e->payload_len > (uint16_t)max_payload) return -1;

    copy_bytes(src_ip_out, e->src_ip, 4);
    *src_port_out = e->src_port;
    *dst_port_out = e->dst_port;
    *out_payload_len = (int)e->payload_len;
    copy_bytes(payload_out, e->payload, e->payload_len);

    e->valid = 0;
    udp_rx_head = (udp_rx_head + 1) % UDP_RX_QUEUE_SIZE;
    udp_rx_count--;
    return 1;
}

int udp_recv_next_for_port(uint16_t dst_port_filter, uint8_t src_ip_out[4], uint16_t* src_port_out, uint16_t* dst_port_out, uint8_t* payload_out, int max_payload, int* out_payload_len) {
    int idx;

    if (!src_ip_out || !src_port_out || !dst_port_out || !payload_out || !out_payload_len) return 0;
    if (max_payload <= 0) return 0;
    if (udp_rx_count == 0) return 0;

    idx = udp_rx_head;
    for (int scanned = 0; scanned < UDP_RX_QUEUE_SIZE; scanned++) {
        UdpRxEntry* e = &udp_rx_queue[idx];
        if (e->valid && e->dst_port == dst_port_filter) {
            if (e->payload_len > (uint16_t)max_payload) return -1;

            copy_bytes(src_ip_out, e->src_ip, 4);
            *src_port_out = e->src_port;
            *dst_port_out = e->dst_port;
            *out_payload_len = (int)e->payload_len;
            copy_bytes(payload_out, e->payload, e->payload_len);

            e->valid = 0;
            udp_rx_count--;

            while (udp_rx_count > 0 && !udp_rx_queue[udp_rx_head].valid) {
                udp_rx_head = (udp_rx_head + 1) % UDP_RX_QUEUE_SIZE;
            }
            if (udp_rx_count == 0) {
                udp_rx_head = udp_rx_tail;
            }

            return 1;
        }
        idx = (idx + 1) % UDP_RX_QUEUE_SIZE;
    }

    return 0;
}

int udp_get_stats(UDPStats* out_stats) {
    if (!out_stats) return 0;
    *out_stats = udp_stats;
    return 1;
}

int udp_set_listen_port(uint16_t port) {
    if (port == 0) return 0;
    udp_listen_enabled = 1;
    udp_listen_port = port;
    return 1;
}

int udp_clear_listen_port(void) {
    udp_listen_enabled = 0;
    udp_listen_port = 0;
    return 1;
}

int udp_get_listen_port(uint16_t* out_port) {
    if (!out_port) return 0;
    *out_port = udp_listen_port;
    return udp_listen_enabled ? 1 : 0;
}
