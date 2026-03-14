#include "kernel.h"

#define ETH_TYPE_IPV4_HI 0x08
#define ETH_TYPE_IPV4_LO 0x00

#define IP_PROTOCOL_ICMP 1

#define ICMP_TYPE_ECHO_REPLY   0
#define ICMP_TYPE_ECHO_REQUEST 8

static ICMPStats icmp_stats;
static uint16_t icmp_next_ip_id = 1;

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

int icmp_send_echo_request(const uint8_t target_ip[4], uint16_t identifier, uint16_t sequence) {
    uint8_t frame[14 + 20 + 8];
    uint8_t* ip;
    uint8_t* icmp;
    uint8_t local_ip[4];
    uint8_t dst_mac[6];
    Rtl8139Status status;

    if (!target_ip) return -1;
    if (!rtl8139_get_status(&status) || !status.initialized) return -2;
    if (!arp_get_local_ip(local_ip)) return -3;
    if (!arp_lookup_mac(target_ip, dst_mac)) return -4;

    copy_bytes(frame + 0, dst_mac, 6);
    copy_bytes(frame + 6, status.mac, 6);
    frame[12] = ETH_TYPE_IPV4_HI;
    frame[13] = ETH_TYPE_IPV4_LO;

    ip = frame + 14;
    ip[0] = 0x45;
    ip[1] = 0x00;
    write_be16(ip + 2, 28);
    write_be16(ip + 4, icmp_next_ip_id++);
    write_be16(ip + 6, 0x0000);
    ip[8] = 64;
    ip[9] = IP_PROTOCOL_ICMP;
    write_be16(ip + 10, 0x0000);
    copy_bytes(ip + 12, local_ip, 4);
    copy_bytes(ip + 16, target_ip, 4);
    write_be16(ip + 10, internet_checksum(ip, 20));

    icmp = ip + 20;
    icmp[0] = ICMP_TYPE_ECHO_REQUEST;
    icmp[1] = 0;
    write_be16(icmp + 2, 0x0000);
    write_be16(icmp + 4, identifier);
    write_be16(icmp + 6, sequence);
    write_be16(icmp + 2, internet_checksum(icmp, 8));

    return rtl8139_send_frame(frame, (int)sizeof(frame));
}

int icmp_poll_once(void) {
    uint8_t frame[1600];
    int length = 0;
    int rx_result = rtl8139_poll_receive(frame, sizeof(frame), &length);

    if (rx_result <= 0) return rx_result;
    return icmp_process_frame(frame, length);
}

int icmp_process_frame(const uint8_t* frame, int length) {
    if (!frame || length <= 0) return -10;

    icmp_stats.frames_polled++;

    if (length < 14 + 20 + 8) return 2;
    if (frame[12] != ETH_TYPE_IPV4_HI || frame[13] != ETH_TYPE_IPV4_LO) return 2;

    {
        const uint8_t* ip = frame + 14;
        uint8_t version = (uint8_t)(ip[0] >> 4);
        uint8_t ihl_words = (uint8_t)(ip[0] & 0x0F);
        int ip_hlen = (int)ihl_words * 4;
        uint16_t total_len = read_be16(ip + 2);

        if (version != 4 || ihl_words < 5 || ip_hlen > 60) {
            icmp_stats.parse_errors++;
            return -1;
        }

        if (14 + total_len > length || total_len < (uint16_t)(ip_hlen + 8)) {
            icmp_stats.parse_errors++;
            return -2;
        }

        if (ip[9] != IP_PROTOCOL_ICMP) return 3;

        {
            const uint8_t* icmp = ip + ip_hlen;
            int icmp_len = total_len - ip_hlen;
            uint8_t local_ip[4];

            if (icmp_len < 8) {
                icmp_stats.parse_errors++;
                return -3;
            }

            icmp_stats.icmp_seen++;

            if (icmp[0] == ICMP_TYPE_ECHO_REQUEST) {
                Rtl8139Status status;
                uint8_t reply[1600];

                icmp_stats.echo_requests++;

                if (!arp_get_local_ip(local_ip)) return -4;
                if (!bytes_equal(ip + 16, local_ip, 4)) return 1;
                if (!rtl8139_get_status(&status) || !status.initialized) return -5;

                copy_bytes(reply + 0, frame + 6, 6);
                copy_bytes(reply + 6, status.mac, 6);
                reply[12] = ETH_TYPE_IPV4_HI;
                reply[13] = ETH_TYPE_IPV4_LO;

                copy_bytes(reply + 14, ip, ip_hlen + icmp_len);

                {
                    uint8_t* rip = reply + 14;
                    uint8_t* ricmp = rip + ip_hlen;

                    copy_bytes(rip + 12, ip + 16, 4);
                    copy_bytes(rip + 16, ip + 12, 4);
                    rip[8] = 64;
                    write_be16(rip + 10, 0x0000);
                    write_be16(rip + 10, internet_checksum(rip, ip_hlen));

                    ricmp[0] = ICMP_TYPE_ECHO_REPLY;
                    ricmp[1] = 0;
                    write_be16(ricmp + 2, 0x0000);
                    write_be16(ricmp + 2, internet_checksum(ricmp, icmp_len));
                }

                if (rtl8139_send_frame(reply, 14 + total_len) > 0) {
                    icmp_stats.echo_replies_sent++;
                    return 1;
                }

                return -6;
            }

            if (icmp[0] == ICMP_TYPE_ECHO_REPLY) {
                icmp_stats.echo_replies_received++;
                return 1;
            }
        }
    }

    return 1;
}

int icmp_get_stats(ICMPStats* out_stats) {
    if (!out_stats) return 0;
    *out_stats = icmp_stats;
    return 1;
}
