static uint16_t bswap16(uint16_t value) {
    return (uint16_t)((value << 8) | (value >> 8));
}

static uint32_t bswap32(uint32_t value) {
    return ((value & 0x000000FFu) << 24) |
           ((value & 0x0000FF00u) << 8) |
           ((value & 0x00FF0000u) >> 8) |
           ((value & 0xFF000000u) >> 24);
}

static uint16_t net_htons(uint16_t value) {
    return bswap16(value);
}

static uint16_t net_ntohs(uint16_t value) {
    return bswap16(value);
}

static uint32_t net_htonl(uint32_t value) {
    return bswap32(value);
}

static uint32_t net_ntohl(uint32_t value) {
    return bswap32(value);
}

static uint16_t read_be16(const uint8_t* data) {
    return (uint16_t)(((uint16_t)data[0] << 8) | (uint16_t)data[1]);
}

static uint32_t read_be32(const uint8_t* data) {
    return ((uint32_t)data[0] << 24) |
           ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) |
           (uint32_t)data[3];
}

static void write_be16(uint8_t* data, uint16_t value) {
    data[0] = (uint8_t)(value >> 8);
    data[1] = (uint8_t)(value & 0xFF);
}

static void write_be32(uint8_t* data, uint32_t value) {
    data[0] = (uint8_t)(value >> 24);
    data[1] = (uint8_t)((value >> 16) & 0xFF);
    data[2] = (uint8_t)((value >> 8) & 0xFF);
    data[3] = (uint8_t)(value & 0xFF);
}

static void serial_write_hex_byte(uint8_t value) {
    const char* hex = "0123456789ABCDEF";
    serial_write_char(hex[value >> 4]);
    serial_write_char(hex[value & 0x0F]);
}

static void serial_write_mac(const uint8_t mac[6]) {
    for (uint8_t i = 0; i < 6; i++) {
        serial_write_hex_byte(mac[i]);
        if (i != 5) {
            serial_write_char(':');
        }
    }
}

static void serial_write_ip(const uint8_t ip[4]) {
    serial_write_dec(ip[0]);
    serial_write_char('.');
    serial_write_dec(ip[1]);
    serial_write_char('.');
    serial_write_dec(ip[2]);
    serial_write_char('.');
    serial_write_dec(ip[3]);
}

static bool ip_is_broadcast(const uint8_t ip[4]) {
    return ip[0] == 255 && ip[1] == 255 && ip[2] == 255 && ip[3] == 255;
}

static bool ip_is_zero(const uint8_t ip[4]) {
    return ip[0] == 0 && ip[1] == 0 && ip[2] == 0 && ip[3] == 0;
}

static uint32_t ip_to_u32(const uint8_t ip[4]) {
    return ((uint32_t)ip[0] << 24) |
           ((uint32_t)ip[1] << 16) |
           ((uint32_t)ip[2] << 8) |
           (uint32_t)ip[3];
}

static void ip_from_u32(uint32_t value, uint8_t ip[4]) {
    ip[0] = (uint8_t)(value >> 24);
    ip[1] = (uint8_t)((value >> 16) & 0xFF);
    ip[2] = (uint8_t)((value >> 8) & 0xFF);
    ip[3] = (uint8_t)(value & 0xFF);
}

static bool ip_equal(const uint8_t a[4], const uint8_t b[4]) {
    return memcmp(a, b, 4) == 0;
}

static char dns_ascii_lower(char c) {
    if (c >= 'A' && c <= 'Z') {
        return (char)(c + ('a' - 'A'));
    }
    return c;
}

static bool dns_hostname_equal_ci(const char* a, const char* b) {
    if (!a || !b) {
        return false;
    }

    size_t i = 0;
    while (a[i] && b[i]) {
        if (dns_ascii_lower(a[i]) != dns_ascii_lower(b[i])) {
            return false;
        }
        i++;
    }
    return a[i] == '\0' && b[i] == '\0';
}

static void dns_hostname_copy_lower(char* dst, size_t dst_size, const char* src) {
    if (!dst || !src || dst_size == 0) {
        return;
    }

    size_t i = 0;
    while (src[i] && i + 1 < dst_size) {
        dst[i] = dns_ascii_lower(src[i]);
        i++;
    }
    dst[i] = '\0';
}

static bool mac_is_broadcast(const uint8_t mac[6]) {
    for (uint8_t i = 0; i < 6; i++) {
        if (mac[i] != 0xFF) {
            return false;
        }
    }
    return true;
}

static bool mac_is_zero(const uint8_t mac[6]) {
    for (uint8_t i = 0; i < 6; i++) {
        if (mac[i] != 0) {
            return false;
        }
    }
    return true;
}

static bool ip_is_same_subnet(const uint8_t a[4], const uint8_t b[4], const uint8_t mask[4]) {
    for (uint8_t i = 0; i < 4; i++) {
        if ((a[i] & mask[i]) != (b[i] & mask[i])) {
            return false;
        }
    }
    return true;
}

static uint64_t arp_cache_ttl_ticks(void) {
    uint32_t frequency = pit_get_frequency();
    if (frequency == 0) {
        return 0;
    }
    return (uint64_t)frequency * ARP_CACHE_TTL_S;
}

static void arp_cache_expire_stale(void) {
    uint64_t ttl = arp_cache_ttl_ticks();
    if (ttl == 0) {
        return;
    }

    uint64_t now = pit_get_ticks();
    for (uint8_t i = 0; i < ARP_CACHE_SIZE; i++) {
        if (!g_arp_cache[i].valid) {
            continue;
        }
        if (now - g_arp_cache[i].updated_at_ticks > ttl) {
            g_arp_cache[i].valid = false;
        }
    }
}

static void arp_cache_clear(void) {
    memset(g_arp_cache, 0, sizeof(g_arp_cache));
}

static bool arp_cache_lookup(const uint8_t ip[4], uint8_t out_mac[6]) {
    if (!ip || !out_mac) {
        return false;
    }

    arp_cache_expire_stale();

    for (uint8_t i = 0; i < ARP_CACHE_SIZE; i++) {
        if (!g_arp_cache[i].valid) {
            continue;
        }
        if (memcmp(g_arp_cache[i].ip, ip, 4) == 0) {
            memcpy(out_mac, g_arp_cache[i].mac, 6);
            g_net_stats.arp_cache_hits++;
            return true;
        }
    }

    g_net_stats.arp_cache_misses++;
    return false;
}

static void arp_cache_update(const uint8_t ip[4], const uint8_t mac[6]) {
    if (!ip || !mac || ip_is_zero(ip) || mac_is_zero(mac) || mac_is_broadcast(mac)) {
        return;
    }

    uint64_t now = pit_get_ticks();
    int free_slot = -1;
    int replace_slot = 0;
    uint64_t oldest_tick = ~(uint64_t)0;

    for (uint8_t i = 0; i < ARP_CACHE_SIZE; i++) {
        if (g_arp_cache[i].valid && memcmp(g_arp_cache[i].ip, ip, 4) == 0) {
            memcpy(g_arp_cache[i].mac, mac, 6);
            g_arp_cache[i].updated_at_ticks = now;
            return;
        }

        if (!g_arp_cache[i].valid && free_slot < 0) {
            free_slot = (int)i;
        }

        if (g_arp_cache[i].valid && g_arp_cache[i].updated_at_ticks < oldest_tick) {
            oldest_tick = g_arp_cache[i].updated_at_ticks;
            replace_slot = (int)i;
        }
    }

    int slot = (free_slot >= 0) ? free_slot : replace_slot;
    g_arp_cache[slot].valid = true;
    memcpy(g_arp_cache[slot].ip, ip, 4);
    memcpy(g_arp_cache[slot].mac, mac, 6);
    g_arp_cache[slot].updated_at_ticks = now;
}

static uint64_t dns_ttl_to_ticks(uint32_t ttl_seconds) {
    uint32_t frequency = pit_get_frequency();
    if (frequency == 0 || ttl_seconds == 0) {
        return 0;
    }
    return (uint64_t)ttl_seconds * frequency;
}

static uint32_t dns_normalize_ttl(uint32_t ttl_seconds) {
    if (ttl_seconds < DNS_MIN_TTL_S) {
        return DNS_MIN_TTL_S;
    }
    if (ttl_seconds > DNS_MAX_TTL_S) {
        return DNS_MAX_TTL_S;
    }
    return ttl_seconds;
}

static void dns_cache_expire_stale(void) {
    uint64_t now = pit_get_ticks();
    for (uint8_t i = 0; i < DNS_CACHE_SIZE; i++) {
        if (!g_dns_cache[i].valid) {
            continue;
        }
        if (g_dns_cache[i].expires_at_ticks != 0 && now >= g_dns_cache[i].expires_at_ticks) {
            g_dns_cache[i].valid = false;
        }
    }
}

static void dns_cache_clear(void) {
    memset(g_dns_cache, 0, sizeof(g_dns_cache));
}

static bool dns_cache_lookup(const char* hostname, uint8_t out_ip[4]) {
    if (!hostname || !out_ip) {
        return false;
    }

    dns_cache_expire_stale();

    for (uint8_t i = 0; i < DNS_CACHE_SIZE; i++) {
        if (!g_dns_cache[i].valid) {
            continue;
        }
        if (dns_hostname_equal_ci(g_dns_cache[i].hostname, hostname)) {
            memcpy(out_ip, g_dns_cache[i].ip, 4);
            g_net_stats.dns_cache_hits++;
            return true;
        }
    }

    g_net_stats.dns_cache_misses++;
    return false;
}

static void dns_cache_update(const char* hostname, const uint8_t ip[4], uint32_t ttl_seconds) {
    if (!hostname || !ip || ip_is_zero(ip) || ip_is_broadcast(ip)) {
        return;
    }

    uint64_t now = pit_get_ticks();
    uint64_t ttl_ticks = dns_ttl_to_ticks(dns_normalize_ttl(ttl_seconds));
    uint64_t expires_at = (ttl_ticks == 0) ? 0 : (now + ttl_ticks);

    int free_slot = -1;
    int replace_slot = 0;
    uint64_t oldest_tick = ~(uint64_t)0;

    for (uint8_t i = 0; i < DNS_CACHE_SIZE; i++) {
        if (g_dns_cache[i].valid && dns_hostname_equal_ci(g_dns_cache[i].hostname, hostname)) {
            memcpy(g_dns_cache[i].ip, ip, 4);
            g_dns_cache[i].updated_at_ticks = now;
            g_dns_cache[i].expires_at_ticks = expires_at;
            return;
        }

        if (!g_dns_cache[i].valid && free_slot < 0) {
            free_slot = (int)i;
        }

        if (g_dns_cache[i].valid && g_dns_cache[i].updated_at_ticks < oldest_tick) {
            oldest_tick = g_dns_cache[i].updated_at_ticks;
            replace_slot = (int)i;
        }
    }

    int slot = (free_slot >= 0) ? free_slot : replace_slot;
    g_dns_cache[slot].valid = true;
    dns_hostname_copy_lower(g_dns_cache[slot].hostname, sizeof(g_dns_cache[slot].hostname), hostname);
    memcpy(g_dns_cache[slot].ip, ip, 4);
    g_dns_cache[slot].updated_at_ticks = now;
    g_dns_cache[slot].expires_at_ticks = expires_at;
}

static uint32_t checksum_add_bytes(uint32_t sum, const uint8_t* data, size_t length) {
    size_t i = 0;
    while (i + 1 < length) {
        sum += ((uint32_t)data[i] << 8) | (uint32_t)data[i + 1];
        i += 2;
    }

    if (i < length) {
        sum += ((uint32_t)data[i] << 8);
    }

    return sum;
}

static uint16_t checksum_finalize(uint32_t sum) {
    while ((sum >> 16) != 0) {
        sum = (sum & 0xFFFFu) + (sum >> 16);
    }
    return (uint16_t)(~sum);
}

static uint16_t ipv4_checksum(const uint8_t* header, size_t length) {
    return checksum_finalize(checksum_add_bytes(0, header, length));
}

static uint16_t udp_checksum(uint32_t src_ip, uint32_t dst_ip, const uint8_t* udp_packet, uint16_t udp_length) {
    uint32_t sum = 0;

    sum += (src_ip >> 16) & 0xFFFFu;
    sum += src_ip & 0xFFFFu;
    sum += (dst_ip >> 16) & 0xFFFFu;
    sum += dst_ip & 0xFFFFu;
    sum += (uint32_t)IP_PROTO_UDP;
    sum += udp_length;

    sum = checksum_add_bytes(sum, udp_packet, udp_length);

    uint16_t result = checksum_finalize(sum);
    return (result == 0) ? 0xFFFFu : result;
}

static uint16_t tcp_checksum(uint32_t src_ip, uint32_t dst_ip, const uint8_t* tcp_packet, uint16_t tcp_length) {
    uint32_t sum = 0;

    sum += (src_ip >> 16) & 0xFFFFu;
    sum += src_ip & 0xFFFFu;
    sum += (dst_ip >> 16) & 0xFFFFu;
    sum += dst_ip & 0xFFFFu;
    sum += (uint32_t)IP_PROTO_TCP;
    sum += tcp_length;

    sum = checksum_add_bytes(sum, tcp_packet, tcp_length);

    return checksum_finalize(sum);
}

