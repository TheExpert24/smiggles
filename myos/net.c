#include "kernel.h"

#define ETH_TYPE_IPV4_HI 0x08
#define ETH_TYPE_IPV4_LO 0x00
#define IP_PROTOCOL_ICMP 1
#define IP_PROTOCOL_TCP 6
#define IP_PROTOCOL_UDP 17

int net_poll_once(void) {
    uint8_t frame[1600];
    int length = 0;
    int rx = rtl8139_poll_receive(frame, sizeof(frame), &length);
    if (rx <= 0) return rx;

    arp_process_frame(frame, length);

    if (length < 14) return 2;
    if (frame[12] != ETH_TYPE_IPV4_HI || frame[13] != ETH_TYPE_IPV4_LO) return 2;
    if (length < 14 + 20) return 2;

    const uint8_t* ip = frame + 14;
    uint8_t protocol = ip[9];

    if (protocol == IP_PROTOCOL_ICMP) {
        icmp_process_frame(frame, length);
        return 3;
    }
    if (protocol == IP_PROTOCOL_UDP) {
        udp_process_frame(frame, length);
        return 3;
    }
    if (protocol == IP_PROTOCOL_TCP) {
        tcp_process_frame(frame, length);
        return 3;
    }

    return 3;
}
