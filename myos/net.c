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

    {
        int ar = arp_process_frame(frame, length);
        if (ar == 1) return 1;
        if (ar < 0) return -100 + ar;
    }

    if (length < 14) return 2;
    if (frame[12] != ETH_TYPE_IPV4_HI || frame[13] != ETH_TYPE_IPV4_LO) return 2;

    if (length < 14 + 20) return 2;

    {
        const uint8_t* ip = frame + 14;
        uint8_t protocol = ip[9];

        if (protocol == IP_PROTOCOL_ICMP) {
            int ir = icmp_process_frame(frame, length);
            if (ir == 1) return 1;
            if (ir < 0) return -200 + ir;
            return 3;
        }

        if (protocol == IP_PROTOCOL_UDP) {
            int ur = udp_process_frame(frame, length);
            if (ur == 1) return 1;
            if (ur < 0) return -300 + ur;
            return 3;
        }

        if (protocol == IP_PROTOCOL_TCP) {
            int tr = tcp_process_frame(frame, length);
            if (tr == 1) return 1;
            if (tr < 0) return -400 + tr;
            return 3;
        }
    }

    return 3;
}
