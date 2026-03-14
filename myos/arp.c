#include "kernel.h"

#define ETH_TYPE_ARP_HI 0x08
#define ETH_TYPE_ARP_LO 0x06
#define ETH_TYPE_IPV4_HI 0x08
#define ETH_TYPE_IPV4_LO 0x00

#define ARP_OP_REQUEST 0x0001u
#define ARP_OP_REPLY   0x0002u

static uint8_t arp_local_ip[4] = {10, 0, 2, 15};
static uint8_t arp_local_ip_set = 1;

static uint8_t arp_cache_valid[ARP_CACHE_SIZE];
static uint8_t arp_cache_ip[ARP_CACHE_SIZE][4];
static uint8_t arp_cache_mac[ARP_CACHE_SIZE][6];
static int arp_cache_next = 0;

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

static void write_be16(uint8_t* p, uint16_t v) {
    p[0] = (uint8_t)((v >> 8) & 0xFF);
    p[1] = (uint8_t)(v & 0xFF);
}

static void arp_cache_store(const uint8_t ip[4], const uint8_t mac[6]) {
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache_valid[i] && bytes_equal(arp_cache_ip[i], ip, 4)) {
            copy_bytes(arp_cache_mac[i], mac, 6);
            return;
        }
    }

    arp_cache_valid[arp_cache_next] = 1;
    copy_bytes(arp_cache_ip[arp_cache_next], ip, 4);
    copy_bytes(arp_cache_mac[arp_cache_next], mac, 6);
    arp_cache_next = (arp_cache_next + 1) % ARP_CACHE_SIZE;
}

static int build_arp_frame(
    uint8_t* frame,
    const uint8_t dst_mac[6],
    const uint8_t src_mac[6],
    uint16_t op,
    const uint8_t sender_mac[6],
    const uint8_t sender_ip[4],
    const uint8_t target_mac[6],
    const uint8_t target_ip[4]
) {
    if (!frame || !dst_mac || !src_mac || !sender_mac || !sender_ip || !target_mac || !target_ip) return 0;

    // Ethernet header
    copy_bytes(frame + 0, dst_mac, 6);
    copy_bytes(frame + 6, src_mac, 6);
    frame[12] = ETH_TYPE_ARP_HI;
    frame[13] = ETH_TYPE_ARP_LO;

    // ARP payload
    write_be16(frame + 14, 0x0001u); // hardware type Ethernet
    write_be16(frame + 16, 0x0800u); // protocol type IPv4
    frame[18] = 6;                   // hardware size
    frame[19] = 4;                   // protocol size
    write_be16(frame + 20, op);
    copy_bytes(frame + 22, sender_mac, 6);
    copy_bytes(frame + 28, sender_ip, 4);
    copy_bytes(frame + 32, target_mac, 6);
    copy_bytes(frame + 38, target_ip, 4);

    for (int i = 42; i < 60; i++) frame[i] = 0;
    return 60;
}

int arp_set_local_ip(const uint8_t ip[4]) {
    if (!ip) return 0;
    copy_bytes(arp_local_ip, ip, 4);
    arp_local_ip_set = 1;
    return 1;
}

int arp_get_local_ip(uint8_t ip_out[4]) {
    if (!ip_out) return 0;
    copy_bytes(ip_out, arp_local_ip, 4);
    return arp_local_ip_set ? 1 : 0;
}

int arp_send_request(const uint8_t target_ip[4]) {
    uint8_t frame[60];
    uint8_t broadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    uint8_t zero_mac[6] = {0, 0, 0, 0, 0, 0};
    Rtl8139Status status;

    if (!target_ip) return -1;
    if (!rtl8139_get_status(&status) || !status.initialized) return -2;
    if (!arp_local_ip_set) return -3;

    if (!build_arp_frame(frame, broadcast, status.mac, ARP_OP_REQUEST, status.mac, arp_local_ip, zero_mac, target_ip)) {
        return -4;
    }

    return rtl8139_send_frame(frame, 60);
}

int arp_poll_once(void) {
    uint8_t frame[1600];
    int length = 0;
    int rx_result = rtl8139_poll_receive(frame, sizeof(frame), &length);

    if (rx_result <= 0) return rx_result;
    return arp_process_frame(frame, length);
}

int arp_process_frame(const uint8_t* frame, int length) {
    if (!frame || length <= 0) return -11;
    if (length < 42) return 0;

    if (frame[12] != ETH_TYPE_ARP_HI || frame[13] != ETH_TYPE_ARP_LO) {
        return 2; // received non-ARP frame
    }

    if (read_be16(frame + 14) != 0x0001u) return -5;
    if (read_be16(frame + 16) != 0x0800u) return -6;
    if (frame[18] != 6 || frame[19] != 4) return -7;

    {
        uint16_t op = read_be16(frame + 20);
        const uint8_t* sender_mac = frame + 22;
        const uint8_t* sender_ip = frame + 28;
        const uint8_t* target_ip = frame + 38;

        arp_cache_store(sender_ip, sender_mac);

        if (op == ARP_OP_REQUEST && arp_local_ip_set && bytes_equal(target_ip, arp_local_ip, 4)) {
            Rtl8139Status status;
            uint8_t reply[60];

            if (!rtl8139_get_status(&status) || !status.initialized) return -8;
            if (!build_arp_frame(reply, sender_mac, status.mac, ARP_OP_REPLY, status.mac, arp_local_ip, sender_mac, sender_ip)) {
                return -9;
            }
            return rtl8139_send_frame(reply, 60) > 0 ? 1 : -10;
        }

        return 1;
    }
}

int arp_get_cache_count(void) {
    int count = 0;
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache_valid[i]) count++;
    }
    return count;
}

int arp_get_cache_entry(int index, uint8_t ip_out[4], uint8_t mac_out[6]) {
    int seen = 0;

    if (index < 0 || !ip_out || !mac_out) return 0;

    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (!arp_cache_valid[i]) continue;
        if (seen == index) {
            copy_bytes(ip_out, arp_cache_ip[i], 4);
            copy_bytes(mac_out, arp_cache_mac[i], 6);
            return 1;
        }
        seen++;
    }

    return 0;
}

int arp_lookup_mac(const uint8_t ip[4], uint8_t mac_out[6]) {
    if (!ip || !mac_out) return 0;

    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (!arp_cache_valid[i]) continue;
        if (bytes_equal(arp_cache_ip[i], ip, 4)) {
            copy_bytes(mac_out, arp_cache_mac[i], 6);
            return 1;
        }
    }

    return 0;
}
