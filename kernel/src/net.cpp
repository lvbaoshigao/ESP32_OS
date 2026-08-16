#include "kernel.h"

// === SLIP (Serial Line IP) Network Driver ===
// ponytail: single-packet rx buffer, no fragmentation. Add ring buffer if throughput matters.
// Uses UART0 in network mode — console is suspended while network is active.

// === SLIP protocol constants ===
#define SLIP_END    0xC0
#define SLIP_ESC    0xDB
#define SLIP_ESC_END   0xDC
#define SLIP_ESC_ESC   0xDD

// === IP/ICMP constants ===
#define IP_PROTO_ICMP   1
#define ICMP_ECHO       8
#define ICMP_ECHO_REPLY 0

// === IP header (20 bytes, no options) ===
struct __attribute__((packed)) ip_hdr_t {
    uint8_t  ver_ihl;       // version (4) + header length (5 = 20 bytes)
    uint8_t  dscp_ecn;
    uint16_t total_len;
    uint16_t id;
    uint16_t flags_frag;
    uint8_t  ttl;
    uint8_t  protocol;
    uint16_t checksum;
    uint32_t src_ip;
    uint32_t dst_ip;
};

// === ICMP header ===
struct __attribute__((packed)) icmp_hdr_t {
    uint8_t  type;
    uint8_t  code;
    uint16_t checksum;
    uint16_t rest[2];       // id + seq for echo
};

// === Network state ===
static int g_net_active = 0;
// Fix 6: store in network byte order (big-endian). 192.168.5.1 = bytes C0 A8 05 01
static uint32_t g_net_ip = 0x0105A8C0;  // 192.168.5.1 in network byte order

// === IP checksum (RFC 1071) ===
static uint16_t ip_checksum(const uint16_t* buf, int words) {
    uint32_t sum = 0;
    for (int i = 0; i < words; i++) sum += buf[i];
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)~sum;
}

// === SLIP framing: send one packet ===
static void slip_send(const uint8_t* data, int len) {
    uart_putc(SLIP_END);  // start frame
    for (int i = 0; i < len; i++) {
        if (data[i] == SLIP_END) {
            uart_putc(SLIP_ESC);
            uart_putc(SLIP_ESC_END);
        } else if (data[i] == SLIP_ESC) {
            uart_putc(SLIP_ESC);
            uart_putc(SLIP_ESC_ESC);
        } else {
            uart_putc((char)data[i]);
        }
    }
    uart_putc(SLIP_END);  // end frame
}

// === SLIP framing: receive one packet (blocking, with yield) ===
// Returns packet length, or 0 on timeout/idle
static int slip_recv(uint8_t* buf, int maxlen) {
    int pos = 0;
    int in_frame = 0;
    int escaped = 0;

    while (1) {
        // Wait for byte with yield
        while (!uart_avail()) {
            task_yield();
        }
        uint8_t c = (uint8_t)uart_getc();

        if (!in_frame) {
            if (c == SLIP_END) in_frame = 1;
            continue;
        }

        // In frame
        if (escaped) {
            if (c == SLIP_ESC_END)      c = SLIP_END;
            else if (c == SLIP_ESC_ESC) c = SLIP_ESC;
            else { /* protocol error, discard */ in_frame = 0; pos = 0; }
            escaped = 0;
            if (pos < maxlen) buf[pos++] = c;
            continue;
        }

        if (c == SLIP_ESC) {
            escaped = 1;
            continue;
        }

        if (c == SLIP_END) {
            // End of frame
            if (pos > 0) return pos;  // return packet length
            in_frame = 0;  // empty frame, restart
            continue;
        }

        if (pos < maxlen) buf[pos++] = c;
    }
}

// === IP handler: called when an IP packet is received ===
static void handle_ip(const uint8_t* pkt, int len) {
    if (len < (int)sizeof(ip_hdr_t)) return;
    const ip_hdr_t* ip = (const ip_hdr_t*)pkt;
    int ihl = (ip->ver_ihl & 0x0F) * 4;
    if (ihl < 20 || ihl > len) return;

    // Only handle packets addressed to us
    if (ip->dst_ip != g_net_ip) return;

    uint16_t calc = ip_checksum((const uint16_t*)ip, ihl / 2);
    if (calc != 0) return;  // bad checksum

    if (ip->protocol == IP_PROTO_ICMP) {
        // ICMP echo request → reply
        const icmp_hdr_t* icmp = (const icmp_hdr_t*)(pkt + ihl);
        int icmp_len = len - ihl;
        if (icmp_len < 8) return;
        if (icmp->type != ICMP_ECHO) return;

        // Fix 4: heap-allocated reply buffer (1536 bytes on stack would overflow 2KB task stack)
        uint8_t* reply = (uint8_t*)mm_alloc(1536);
        if (!reply) return;
        ip_hdr_t* rip = (ip_hdr_t*)reply;
        icmp_hdr_t* ricmp = (icmp_hdr_t*)(reply + ihl);

        // Copy entire packet
        k_memcpy(reply, pkt, len);

        // Swap IPs
        rip->dst_ip = ip->src_ip;
        rip->src_ip = g_net_ip;

        // Decrement TTL
        if (rip->ttl > 0) rip->ttl--;

        // Clear IP checksum and recalculate
        rip->checksum = 0;
        rip->checksum = ip_checksum((const uint16_t*)rip, ihl / 2);

        // ICMP: change type, recalculate checksum
        ricmp->type = ICMP_ECHO_REPLY;
        ricmp->checksum = 0;
        uint32_t icmp_sum = 0;
        int icmp_words = icmp_len / 2;
        const uint16_t* icmp_w = (const uint16_t*)ricmp;
        for (int i = 0; i < icmp_words; i++) icmp_sum += icmp_w[i];
        if (icmp_len & 1) icmp_sum += ((const uint8_t*)ricmp)[icmp_len - 1];
        while (icmp_sum >> 16) icmp_sum = (icmp_sum & 0xFFFF) + (icmp_sum >> 16);
        ricmp->checksum = (uint16_t)~icmp_sum;

        // Send reply
        slip_send(reply, len);
        mm_free(reply);
    }
}

// === Called from scheduler: check for incoming SLIP frames ===
// ponytail: no background read — SLIP frames are received synchronously
// in net_read(). This avoids UART contention with the console.
void net_poll(void) {
    // No-op: SLIP frames are only received when explicitly requested.
    // The ping command calls net_read() which calls slip_recv().
}

// === Driver interface ===

static int net_init_impl(void) {
    g_net_active = 0;
    uart_puts("[NET] SLIP driver ready (192.168.5.1)\r\n");
    return 0;
}

static int net_read_impl(void* buf, int len) {
    if (!g_net_active || !buf || len <= 0) return -1;
    int rlen = slip_recv((uint8_t*)buf, len);
    if (rlen > 0) {
        // Let handle_ip() process any packets that need a response
        handle_ip((const uint8_t*)buf, rlen);
    }
    return rlen > 0 ? rlen : -1;
}

static int net_write_impl(const void* buf, int len) {
    if (!g_net_active || !buf || len <= 0) return -1;
    slip_send((const uint8_t*)buf, len);
    return len;
}

static int net_ioctl_impl(int cmd, void* arg) {
    (void)cmd;
    (void)arg;
    return -1;  // no ioctls yet
}

// === Network activation ===
void net_activate(void) {
    g_net_active = 1;
    // Flush any stale UART data
    while (uart_avail()) uart_getc();
}

void net_deactivate(void) {
    g_net_active = 0;
}

int net_is_active(void) {
    return g_net_active;
}

// === Driver struct (exported to espsys) ===
driver_t g_net_driver = {
    "network", net_init_impl, net_read_impl, net_write_impl, net_ioctl_impl, 0
};