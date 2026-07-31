#include "kernel.h"

struct dns_header {
    uint16_t id;
    uint16_t flags;
    uint16_t q_count;
    uint16_t ans_count;
    uint16_t auth_count;
    uint16_t add_count;
} __attribute__((packed));

static uint16_t read_be16(const uint8_t* p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static void write_be16(uint8_t* p, uint16_t v) {
    p[0] = (uint8_t)((v >> 8) & 0xFF);
    p[1] = (uint8_t)(v & 0xFF);
}

static int dns_encode_name(uint8_t* dest, const char* name) {
    int dest_idx = 0;
    int src_idx = 0;
    while (name[src_idx] != '\0') {
        int len_idx = dest_idx;
        dest_idx++;
        int count = 0;
        while (name[src_idx] != '.' && name[src_idx] != '\0') {
            *(dest + dest_idx) = (uint8_t)name[src_idx];
            dest_idx++;
            src_idx++;
            count++;
        }
        *(dest + len_idx) = (uint8_t)count;
        if (name[src_idx] == '.') {
            src_idx++;
        }
    }
    *(dest + dest_idx) = 0;
    dest_idx++;
    return dest_idx;
}


int dns_send_query(const uint8_t dns_server_ip[4], const char* hostname, uint16_t query_id) {
    static uint8_t payload[512];
    struct dns_header* h = (struct dns_header*)payload;
    
    write_be16((uint8_t*)&h->id, query_id);
    write_be16((uint8_t*)&h->flags, 0x0100);
    write_be16((uint8_t*)&h->q_count, 1);
    write_be16((uint8_t*)&h->ans_count, 0);
    write_be16((uint8_t*)&h->auth_count, 0);
    write_be16((uint8_t*)&h->add_count, 0);

    int offset = 12;
    offset += dns_encode_name(payload + offset, hostname);
    
    write_be16(payload + offset, 0x0001);
    offset += 2;
    write_be16(payload + offset, 0x0001);
    offset += 2;

    return udp_send_datagram(dns_server_ip, 54321, 53, payload, offset);
}


static int dns_skip_name(const uint8_t* payload, int payload_len, int offset) {
    while (offset < payload_len) {
        uint8_t val = payload[offset];
        if ((val & 0xC0) == 0xC0) {
            return offset + 2;
        }
        if (val == 0) {
            return offset + 1;
        }
        offset += (int)val + 1;
    }
    return -1;
}

int dns_parse_response(const uint8_t* payload, int payload_len, uint16_t expected_id, uint8_t resolved_ip_out[4]) {
    if (payload_len < 12) return -1;
    
    uint16_t id = read_be16(payload + 0);
    uint16_t flags = read_be16(payload + 2);
    uint16_t q_count = read_be16(payload + 4);
    uint16_t ans_count = read_be16(payload + 6);
    
    if (id != expected_id) return -2;
    if ((flags & 0x8000) == 0) return -3;
    if ((flags & 0x000F) != 0) return -4;
    if (q_count == 0 || ans_count == 0) return -5;

    int offset = 12;
    for (int i = 0; i < q_count; i++) {
        offset = dns_skip_name(payload, payload_len, offset);
        if (offset < 0 || offset + 4 > payload_len) return -6;
        offset += 4;
    }

    for (int i = 0; i < ans_count; i++) {
        offset = dns_skip_name(payload, payload_len, offset);
        if (offset < 0 || offset + 10 > payload_len) return -7;
        
        uint16_t type = read_be16(payload + offset);
        uint16_t class = read_be16(payload + offset + 2);
        uint16_t rdlength = read_be16(payload + offset + 8);
        offset += 10;
        
        if (offset + rdlength > payload_len) return -8;
        
        if (type == 0x0001 && class == 0x0001 && rdlength == 4) {
            resolved_ip_out[0] = payload[offset];
            resolved_ip_out[1] = payload[offset + 1];
            resolved_ip_out[2] = payload[offset + 2];
            resolved_ip_out[3] = payload[offset + 3];
            return 1;
        }
        offset += rdlength;
    }
    return -9;
}
int resolve_domain_example(const char* target_domain, uint8_t* out_ip) {
    uint8_t dns_server[4] = {10, 0, 0, 1}; 
    uint16_t q_id = 0xAA55;
    uint8_t rx_buffer[512];
    uint8_t src_ip[4];
    uint16_t src_port;
    int rx_len = 0;

    int fd = sock_open_udp();
    if (fd < 0) return -10;

    sock_bind(fd, 54321);

    if (dns_send_query(dns_server, target_domain, q_id) <= 0) {
        sock_close(fd);
        return -1;
    }

    int res = sock_recvfrom(fd, src_ip, &src_port, rx_buffer, sizeof(rx_buffer), &rx_len);
    sock_close(fd);

    if (res > 0) {
        if (dns_parse_response(rx_buffer, rx_len, q_id, out_ip) == 1) {
            return 1;
        }
    }
    return -2;
}
