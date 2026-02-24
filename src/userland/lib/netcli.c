#include <stdio.h>
#include <string.h>
#include <syscall.h>
#include <netcli.h>

static char hex_nibble(uint8_t value) {
    return (value < 10) ? (char)('0' + value) : (char)('A' + (value - 10));
}

static void print_hex_byte(uint8_t value) {
    putchar(hex_nibble((uint8_t)(value >> 4)));
    putchar(hex_nibble((uint8_t)(value & 0x0F)));
}

void netcli_print_ip4(const uint8_t ip[4]) {
    printf("%u.%u.%u.%u", (unsigned)ip[0], (unsigned)ip[1], (unsigned)ip[2], (unsigned)ip[3]);
}

void netcli_print_mac(const uint8_t mac[6]) {
    for (int i = 0; i < 6; i++) {
        print_hex_byte(mac[i]);
        if (i != 5) putchar(':');
    }
}

int netcli_parse_ipv4(const char* s, uint8_t out[4]) {
    if (!s || !out) return -1;

    const char* p = s;
    for (int part = 0; part < 4; part++) {
        if (*p < '0' || *p > '9') return -1;
        unsigned value = 0;
        int digits = 0;

        while (*p >= '0' && *p <= '9') {
            value = value * 10 + (unsigned)(*p - '0');
            if (value > 255) return -1;
            p++;
            digits++;
        }

        if (digits == 0) return -1;
        out[part] = (uint8_t)value;

        if (part < 3) {
            if (*p != '.') return -1;
            p++;
        }
    }

    return (*p == '\0') ? 0 : -1;
}

bool netcli_contains_char(const char* s, char needle) {
    if (!s) return false;
    while (*s) {
        if (*s == needle) return true;
        s++;
    }
    return false;
}

bool netcli_ip4_is_zero(const uint8_t ip[4]) {
    return ip[0] == 0 && ip[1] == 0 && ip[2] == 0 && ip[3] == 0;
}

int netcli_resolve_target_ipv4(const char* target, uint8_t out_ip[4], bool* out_from_dns, const char* context) {
    if (!target || !out_ip) return -1;

    if (netcli_parse_ipv4(target, out_ip) == 0) {
        if (out_from_dns) *out_from_dns = false;
        return 0;
    }

    const uint32_t dns_timeouts_ms[] = {3000u, 5000u, 8000u};
    for (size_t i = 0; i < sizeof(dns_timeouts_ms) / sizeof(dns_timeouts_ms[0]); i++) {
        if (sys_net_resolve(target, dns_timeouts_ms[i], out_ip) == 0) {
            if (out_from_dns) *out_from_dns = true;
            return 0;
        }
        if (i + 1 < sizeof(dns_timeouts_ms) / sizeof(dns_timeouts_ms[0])) {
            sys_sleep(120);
        }
    }

    if (!context) context = "net";
    printf("%s: invalid IPv4 address or unknown host\n", context);
    if (netcli_contains_char(target, ',')) {
        printf("hint: use '.' instead of ','\n");
    } else {
        printf("hint: DNS may be temporary unavailable; try again\n");
    }
    return -1;
}

const char* netcli_tcp_service_name(uint16_t port) {
    switch (port) {
        case 21: return "ftp";
        case 22: return "ssh";
        case 23: return "telnet";
        case 25: return "smtp";
        case 53: return "dns";
        case 80: return "http";
        case 110: return "pop3";
        case 143: return "imap";
        case 443: return "https";
        case 465: return "smtps";
        case 587: return "submission";
        case 993: return "imaps";
        case 995: return "pop3s";
        case 3306: return "mysql";
        case 3389: return "rdp";
        case 5432: return "postgres";
        case 6379: return "redis";
        case 8080: return "http-alt";
        case 8443: return "https-alt";
        default: return NULL;
    }
}

int netcli_run_tcp_probe_once(const uint8_t ip[4], uint16_t port, uint32_t timeout_ms,
                              bool* out_ok, bool* out_open, uint32_t* out_rtt_ms) {
    if (!ip || !out_ok || !out_open || !out_rtt_ms || port == 0) {
        return -1;
    }

    struct net_tcp_probe_req req;
    struct net_tcp_probe_rsp rsp;
    memset(&req, 0, sizeof(req));
    memset(&rsp, 0, sizeof(rsp));
    memcpy(req.dst_ip, ip, 4);
    req.dst_port = port;
    req.timeout_ms = timeout_ms;

    if (sys_net_tcp_probe(&req, &rsp) < 0) {
        return -1;
    }

    *out_ok = (rsp.ok != 0);
    *out_open = (rsp.open != 0);
    *out_rtt_ms = rsp.rtt_ms;
    return 0;
}
