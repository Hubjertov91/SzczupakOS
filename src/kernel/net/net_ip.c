static bool net_send_ethernet_frame(const uint8_t dst_mac[6], uint16_t ethertype,
                                    const uint8_t* payload, size_t payload_length) {
    if (!g_rtl8139.initialized || !dst_mac || !payload || payload_length > ETH_MTU) {
        return false;
    }

    eth_header_t* eth = (eth_header_t*)g_tx_eth_frame;
    memcpy(eth->dst_mac, dst_mac, 6);
    memcpy(eth->src_mac, g_rtl8139.mac, 6);
    eth->ethertype = net_htons(ethertype);

    if (payload_length > 0) {
        memcpy(g_tx_eth_frame + ETH_HEADER_SIZE, payload, payload_length);
    }

    bool ok = rtl8139_send_frame(g_tx_eth_frame, ETH_HEADER_SIZE + payload_length);
    if (ok) {
        g_net_stats.tx_frames++;
        if (ethertype == ETH_TYPE_IPV4) {
            g_net_stats.tx_ipv4++;
        } else if (ethertype == ETH_TYPE_ARP) {
            g_net_stats.tx_arp++;
        }
    }
    return ok;
}

static bool arp_send_packet(uint16_t opcode, const uint8_t target_mac[6], const uint8_t target_ip[4]) {
    if (!target_ip || !g_rtl8139.initialized || ip_is_zero(g_net_config.ip)) {
        return false;
    }

    arp_packet_t packet;
    memset(&packet, 0, sizeof(packet));
    packet.hardware_type = net_htons(ARP_HTYPE_ETHERNET);
    packet.protocol_type = net_htons(ARP_PTYPE_IPV4);
    packet.hardware_size = ARP_HLEN_ETHERNET;
    packet.protocol_size = ARP_PLEN_IPV4;
    packet.opcode = net_htons(opcode);
    memcpy(packet.sender_mac, g_rtl8139.mac, 6);
    memcpy(packet.sender_ip, g_net_config.ip, 4);
    memcpy(packet.target_ip, target_ip, 4);

    uint8_t dst_mac[6];
    if (opcode == ARP_OP_REQUEST) {
        memset(packet.target_mac, 0, 6);
        memset(dst_mac, 0xFF, 6);
    } else if (opcode == ARP_OP_REPLY) {
        if (!target_mac) {
            return false;
        }
        memcpy(packet.target_mac, target_mac, 6);
        memcpy(dst_mac, target_mac, 6);
    } else {
        return false;
    }

    return net_send_ethernet_frame(dst_mac, ETH_TYPE_ARP, (const uint8_t*)&packet, sizeof(packet));
}

static bool arp_send_request(const uint8_t target_ip[4]) {
    return arp_send_packet(ARP_OP_REQUEST, NULL, target_ip);
}

static bool arp_send_reply(const uint8_t target_mac[6], const uint8_t target_ip[4]) {
    return arp_send_packet(ARP_OP_REPLY, target_mac, target_ip);
}

static bool net_pick_next_hop(const uint8_t dst_ip[4], uint8_t next_hop_ip[4], bool* out_broadcast_mac) {
    if (!dst_ip || !next_hop_ip || !out_broadcast_mac) {
        return false;
    }

    if (ip_is_broadcast(dst_ip)) {
        memcpy(next_hop_ip, dst_ip, 4);
        *out_broadcast_mac = true;
        return true;
    }

    if (!g_net_config.configured || ip_is_zero(g_net_config.ip)) {
        return false;
    }

    if (ip_is_zero(g_net_config.netmask) || ip_is_same_subnet(dst_ip, g_net_config.ip, g_net_config.netmask)) {
        memcpy(next_hop_ip, dst_ip, 4);
        *out_broadcast_mac = false;
        return true;
    }

    if (!ip_is_zero(g_net_config.gateway)) {
        memcpy(next_hop_ip, g_net_config.gateway, 4);
        *out_broadcast_mac = false;
        return true;
    }

    return false;
}

static bool arp_resolve(const uint8_t target_ip[4], uint8_t out_mac[6], uint32_t timeout_ms) {
    if (!target_ip || !out_mac) {
        return false;
    }

    if (ip_is_broadcast(target_ip)) {
        memset(out_mac, 0xFF, 6);
        return true;
    }

    if (arp_cache_lookup(target_ip, out_mac)) {
        return true;
    }

    if (timeout_ms == 0) {
        timeout_ms = 1200;
    }

    if (!arp_send_request(target_ip)) {
        return false;
    }

    uint32_t frequency = pit_get_frequency();
    if (frequency == 0) {
        for (uint32_t i = 0; i < 300000u; i++) {
            net_poll();
            if (arp_cache_lookup(target_ip, out_mac)) {
                return true;
            }
            __asm__ volatile("pause");
        }
        return false;
    }

    uint64_t now = pit_get_ticks();
    uint64_t timeout_ticks = ((uint64_t)timeout_ms * frequency + 999u) / 1000u;
    if (timeout_ticks == 0) {
        timeout_ticks = 1;
    }
    uint64_t deadline = now + timeout_ticks;
    uint64_t retry_ticks = frequency / 5u;
    if (retry_ticks == 0) {
        retry_ticks = 1;
    }
    uint64_t retry_at = now + retry_ticks;

    while (pit_get_ticks() <= deadline) {
        net_poll();
        if (arp_cache_lookup(target_ip, out_mac)) {
            return true;
        }

        now = pit_get_ticks();
        if (now >= retry_at) {
            arp_send_request(target_ip);
            retry_at = now + retry_ticks;
        }

        __asm__ volatile("pause");
    }

    return arp_cache_lookup(target_ip, out_mac);
}

static bool net_resolve_dest_mac(const uint8_t dst_ip[4], bool force_broadcast_eth, uint8_t out_mac[6]) {
    if (!dst_ip || !out_mac) {
        return false;
    }

    if (force_broadcast_eth || ip_is_broadcast(dst_ip)) {
        memset(out_mac, 0xFF, 6);
        return true;
    }

    if (g_net_config.configured && ip_equal(dst_ip, g_net_config.ip)) {
        memcpy(out_mac, g_rtl8139.mac, 6);
        return true;
    }

    uint8_t next_hop_ip[4];
    bool broadcast_mac = false;
    if (!net_pick_next_hop(dst_ip, next_hop_ip, &broadcast_mac)) {
        return false;
    }

    if (broadcast_mac) {
        memset(out_mac, 0xFF, 6);
        return true;
    }

    if (!arp_resolve(next_hop_ip, out_mac, NET_DEFAULT_PING_TIMEOUT_MS)) {
        serial_write("[NET] ARP resolve timeout for ");
        serial_write_ip(next_hop_ip);
        serial_write("\n");
        return false;
    }

    return true;
}

static bool net_send_ipv4_payload(const uint8_t src_ip[4], const uint8_t dst_ip[4],
                                  uint8_t protocol, uint8_t ttl, const uint8_t* payload,
                                  size_t payload_length, bool force_broadcast_eth) {
    if (!src_ip || !dst_ip || !payload) {
        return false;
    }

    if (payload_length > (ETH_MTU - sizeof(ipv4_header_t))) {
        return false;
    }

    uint8_t dst_mac[6];
    if (!net_resolve_dest_mac(dst_ip, force_broadcast_eth, dst_mac)) {
        return false;
    }

    memset(g_tx_ipv4_packet, 0, sizeof(g_tx_ipv4_packet));

    ipv4_header_t* ip = (ipv4_header_t*)g_tx_ipv4_packet;
    ip->version_ihl = 0x45;
    ip->dscp_ecn = 0;
    uint16_t ip_total_length = (uint16_t)(sizeof(ipv4_header_t) + payload_length);
    ip->total_length = net_htons(ip_total_length);
    ip->identification = net_htons(g_ip_identification++);
    ip->flags_fragment = net_htons(0x4000u);
    ip->ttl = (ttl == 0) ? 64 : ttl;
    ip->protocol = protocol;
    ip->header_checksum = 0;
    ip->src_ip = net_htonl(ip_to_u32(src_ip));
    ip->dst_ip = net_htonl(ip_to_u32(dst_ip));
    ip->header_checksum = net_htons(ipv4_checksum((const uint8_t*)ip, sizeof(ipv4_header_t)));

    memcpy(g_tx_ipv4_packet + sizeof(ipv4_header_t), payload, payload_length);

    return net_send_ethernet_frame(dst_mac, ETH_TYPE_IPV4, g_tx_ipv4_packet, ip_total_length);
}

static bool net_send_udp_ipv4(const uint8_t src_ip[4], const uint8_t dst_ip[4],
                              uint16_t src_port, uint16_t dst_port,
                              const uint8_t* payload, size_t payload_length,
                              bool force_broadcast_eth) {
    if (!g_rtl8139.initialized || !src_ip || !dst_ip || !payload) {
        return false;
    }

    if (payload_length > (ETH_MTU - sizeof(ipv4_header_t) - sizeof(udp_header_t))) {
        return false;
    }

    udp_header_t* udp = (udp_header_t*)g_tx_udp_packet;
    udp->src_port = net_htons(src_port);
    udp->dst_port = net_htons(dst_port);
    uint16_t udp_length = (uint16_t)(sizeof(udp_header_t) + payload_length);
    udp->length = net_htons(udp_length);
    udp->checksum = 0;

    uint8_t* udp_payload = g_tx_udp_packet + sizeof(udp_header_t);
    memcpy(udp_payload, payload, payload_length);

    udp->checksum = net_htons(udp_checksum(ip_to_u32(src_ip), ip_to_u32(dst_ip), g_tx_udp_packet, udp_length));

    bool ok = net_send_ipv4_payload(src_ip, dst_ip, IP_PROTO_UDP, 64, g_tx_udp_packet, udp_length, force_broadcast_eth);
    if (ok) {
        g_net_stats.tx_udp++;
    }
    return ok;
}

static bool net_send_tcp_ipv4(const uint8_t src_ip[4], const uint8_t dst_ip[4],
                              uint16_t src_port, uint16_t dst_port,
                              uint32_t sequence, uint32_t ack_number,
                              uint8_t flags, uint8_t ttl,
                              const uint8_t* payload, size_t payload_length) {
    if (!g_rtl8139.initialized || !src_ip || !dst_ip || src_port == 0 || dst_port == 0) {
        return false;
    }

    if ((payload_length > 0 && !payload) ||
        payload_length > (ETH_MTU - sizeof(ipv4_header_t) - sizeof(tcp_header_t))) {
        return false;
    }

    tcp_header_t* tcp = (tcp_header_t*)g_tx_tcp_packet;
    memset(tcp, 0, sizeof(*tcp));
    tcp->src_port = net_htons(src_port);
    tcp->dst_port = net_htons(dst_port);
    tcp->sequence_number = net_htonl(sequence);
    tcp->ack_number = net_htonl(ack_number);
    tcp->data_offset_reserved = (uint8_t)(5u << 4);
    tcp->flags = flags;
    tcp->window_size = net_htons(0xFFFFu);
    tcp->checksum = 0;
    tcp->urgent_pointer = 0;

    uint16_t tcp_length = (uint16_t)(sizeof(tcp_header_t) + payload_length);
    if (payload_length > 0) {
        memcpy(g_tx_tcp_packet + sizeof(tcp_header_t), payload, payload_length);
    }

    tcp->checksum = net_htons(tcp_checksum(ip_to_u32(src_ip), ip_to_u32(dst_ip),
                                           (const uint8_t*)tcp, tcp_length));

    return net_send_ipv4_payload(src_ip, dst_ip, IP_PROTO_TCP, ttl,
                                 (const uint8_t*)tcp, tcp_length, false);
}
