static void net_handle_ipv4(const uint8_t* packet, size_t length, const uint8_t src_mac[6]) {
    if (!packet || length < sizeof(ipv4_header_t)) {
        return;
    }

    const ipv4_header_t* ip = (const ipv4_header_t*)packet;
    uint8_t version = (uint8_t)(ip->version_ihl >> 4);
    uint8_t ihl_words = (uint8_t)(ip->version_ihl & 0x0F);
    size_t ihl = (size_t)ihl_words * 4u;

    if (version != 4 || ihl < sizeof(ipv4_header_t) || ihl > length) {
        return;
    }

    uint16_t total_length = net_ntohs(ip->total_length);
    if (total_length < ihl || total_length > length) {
        return;
    }

    if (ipv4_checksum(packet, ihl) != 0) {
        return;
    }

    uint32_t src_ip = net_ntohl(ip->src_ip);
    uint32_t dst_ip = net_ntohl(ip->dst_ip);

    uint8_t src_ip_bytes[4];
    ip_from_u32(src_ip, src_ip_bytes);
    if (src_mac && !mac_is_zero(src_mac) && !mac_is_broadcast(src_mac) &&
        !ip_is_zero(src_ip_bytes) && !ip_is_broadcast(src_ip_bytes)) {
        arp_cache_update(src_ip_bytes, src_mac);
    }

    uint8_t dst_ip_bytes[4];
    ip_from_u32(dst_ip, dst_ip_bytes);

    bool for_us = false;
    if (!ip_is_zero(g_net_config.ip) && memcmp(dst_ip_bytes, g_net_config.ip, 4) == 0) {
        for_us = true;
    }
    if (ip_is_broadcast(dst_ip_bytes)) {
        for_us = true;
    }
    if (!for_us) {
        return;
    }

    const uint8_t* payload = packet + ihl;
    size_t payload_length = (size_t)total_length - ihl;

    if (ip->protocol == IP_PROTO_ICMP) {
        net_handle_icmp(payload, payload_length, src_ip, dst_ip, src_mac);
    } else if (ip->protocol == IP_PROTO_UDP) {
        net_handle_udp(payload, payload_length, src_ip, dst_ip);
    } else if (ip->protocol == IP_PROTO_TCP) {
        net_handle_tcp(payload, payload_length, src_ip, dst_ip);
    }
}

static void net_handle_ethernet(const uint8_t* frame, size_t length) {
    if (!frame || length < sizeof(eth_header_t)) {
        return;
    }

    const eth_header_t* eth = (const eth_header_t*)frame;
    if (!mac_is_broadcast(eth->dst_mac) && memcmp(eth->dst_mac, g_rtl8139.mac, 6) != 0) {
        return;
    }

    uint16_t ethertype = net_ntohs(eth->ethertype);
    const uint8_t* payload = frame + sizeof(eth_header_t);
    size_t payload_length = length - sizeof(eth_header_t);

    if (ethertype == ETH_TYPE_IPV4) {
        g_net_stats.rx_ipv4++;
        net_handle_ipv4(payload, payload_length, eth->src_mac);
    } else if (ethertype == ETH_TYPE_ARP) {
        g_net_stats.rx_arp++;
        net_handle_arp(payload, payload_length);
    }
}
