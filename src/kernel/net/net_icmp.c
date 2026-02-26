static uint16_t icmp_checksum(const uint8_t* packet, size_t length) {
    return checksum_finalize(checksum_add_bytes(0, packet, length));
}

static bool icmp_match_embedded_probe(const uint8_t* payload, size_t payload_length,
                                      uint16_t identifier, uint16_t sequence,
                                      uint32_t expected_dst_ip, uint32_t expected_src_ip) {
    if (!payload || payload_length < 8u + sizeof(ipv4_header_t)) {
        return false;
    }

    const uint8_t* embedded_packet = payload + 8u;
    size_t embedded_length = payload_length - 8u;
    const ipv4_header_t* embedded_ip = (const ipv4_header_t*)embedded_packet;

    uint8_t version = (uint8_t)(embedded_ip->version_ihl >> 4);
    uint8_t ihl_words = (uint8_t)(embedded_ip->version_ihl & 0x0Fu);
    size_t ihl = (size_t)ihl_words * 4u;
    if (version != 4 || ihl < sizeof(ipv4_header_t) || ihl > embedded_length) {
        return false;
    }

    if (embedded_ip->protocol != IP_PROTO_ICMP) {
        return false;
    }

    if (embedded_length < ihl + sizeof(icmp_echo_header_t)) {
        return false;
    }

    uint32_t embedded_src_ip = net_ntohl(embedded_ip->src_ip);
    uint32_t embedded_dst_ip = net_ntohl(embedded_ip->dst_ip);
    if (embedded_src_ip != expected_src_ip || embedded_dst_ip != expected_dst_ip) {
        return false;
    }

    const icmp_echo_header_t* embedded_icmp = (const icmp_echo_header_t*)(embedded_packet + ihl);
    if (embedded_icmp->type != ICMP_TYPE_ECHO_REQUEST || embedded_icmp->code != 0) {
        return false;
    }

    return net_ntohs(embedded_icmp->identifier) == identifier &&
           net_ntohs(embedded_icmp->sequence) == sequence;
}

static void net_handle_icmp(const uint8_t* payload, size_t payload_length, uint32_t src_ip, uint32_t dst_ip,
                            const uint8_t src_mac[6]) {
    if (!payload || payload_length < sizeof(icmp_echo_header_t)) {
        return;
    }

    if (icmp_checksum(payload, payload_length) != 0) {
        return;
    }

    g_net_stats.rx_icmp++;

    const icmp_echo_header_t* icmp = (const icmp_echo_header_t*)payload;
    uint8_t icmp_type = icmp->type;
    uint8_t icmp_code = icmp->code;

    if (icmp_type == ICMP_TYPE_ECHO_REQUEST && icmp_code == 0) {
        if (!g_net_config.configured || ip_is_zero(g_net_config.ip)) {
            return;
        }

        if (!src_mac || mac_is_zero(src_mac) || mac_is_broadcast(src_mac)) {
            return;
        }

        uint8_t dst_ip_bytes[4];
        ip_from_u32(dst_ip, dst_ip_bytes);
        if (memcmp(dst_ip_bytes, g_net_config.ip, 4) != 0) {
            return;
        }

        if (payload_length > (ETH_MTU - sizeof(ipv4_header_t))) {
            return;
        }

        memcpy(g_icmp_reply, payload, payload_length);

        icmp_echo_header_t* reply_hdr = (icmp_echo_header_t*)g_icmp_reply;
        reply_hdr->type = ICMP_TYPE_ECHO_REPLY;
        reply_hdr->code = 0;
        reply_hdr->checksum = 0;
        reply_hdr->checksum = net_htons(icmp_checksum(g_icmp_reply, payload_length));

        ipv4_header_t* ip = (ipv4_header_t*)g_icmp_ipv4_packet;
        memset(ip, 0, sizeof(*ip));
        ip->version_ihl = 0x45;
        uint16_t ip_total_length = (uint16_t)(sizeof(ipv4_header_t) + payload_length);
        ip->total_length = net_htons(ip_total_length);
        ip->identification = net_htons(g_ip_identification++);
        ip->flags_fragment = net_htons(0x4000u);
        ip->ttl = 64;
        ip->protocol = IP_PROTO_ICMP;
        ip->src_ip = net_htonl(ip_to_u32(g_net_config.ip));
        ip->dst_ip = net_htonl(src_ip);
        ip->header_checksum = net_htons(ipv4_checksum((const uint8_t*)ip, sizeof(ipv4_header_t)));
        memcpy(g_icmp_ipv4_packet + sizeof(ipv4_header_t), g_icmp_reply, payload_length);

        uint8_t src_ip_bytes[4];
        ip_from_u32(src_ip, src_ip_bytes);
        arp_cache_update(src_ip_bytes, src_mac);
        if (net_send_ethernet_frame(src_mac, ETH_TYPE_IPV4, g_icmp_ipv4_packet, ip_total_length)) {
            g_net_stats.tx_icmp++;
        }
        return;
    }

    if (icmp_type == ICMP_TYPE_ECHO_REPLY && icmp_code == 0) {
        uint16_t identifier = net_ntohs(icmp->identifier);
        uint16_t sequence = net_ntohs(icmp->sequence);

        if (g_ping_state.waiting &&
            !g_ping_state.received &&
            identifier == g_ping_state.identifier &&
            sequence == g_ping_state.sequence &&
            src_ip == g_ping_state.expected_src_ip) {
            g_ping_state.received = true;
            g_ping_state.received_tick = pit_get_ticks();
            g_ping_state.reply_src_ip = src_ip;
            g_ping_state.reply_icmp_type = ICMP_TYPE_ECHO_REPLY;
        }
        return;
    }

    if (icmp_type == ICMP_TYPE_TIME_EXCEEDED && g_ping_state.waiting &&
        !g_ping_state.received && g_ping_state.accept_time_exceeded) {
        uint32_t expected_src_ip = ip_to_u32(g_net_config.ip);
        if (icmp_match_embedded_probe(payload, payload_length,
                                      g_ping_state.identifier, g_ping_state.sequence,
                                      g_ping_state.expected_src_ip, expected_src_ip)) {
            g_ping_state.received = true;
            g_ping_state.received_tick = pit_get_ticks();
            g_ping_state.reply_src_ip = src_ip;
            g_ping_state.reply_icmp_type = ICMP_TYPE_TIME_EXCEEDED;
        }
    }
}

static void net_handle_arp(const uint8_t* packet, size_t length) {
    if (!packet || length < sizeof(arp_packet_t)) {
        return;
    }

    const arp_packet_t* arp = (const arp_packet_t*)packet;
    if (net_ntohs(arp->hardware_type) != ARP_HTYPE_ETHERNET ||
        net_ntohs(arp->protocol_type) != ARP_PTYPE_IPV4 ||
        arp->hardware_size != ARP_HLEN_ETHERNET ||
        arp->protocol_size != ARP_PLEN_IPV4) {
        return;
    }

    uint16_t opcode = net_ntohs(arp->opcode);

    bool valid_sender = !ip_is_zero(arp->sender_ip) &&
                        !mac_is_zero(arp->sender_mac) &&
                        !mac_is_broadcast(arp->sender_mac);
    if (valid_sender) {
        arp_cache_update(arp->sender_ip, arp->sender_mac);
    }

    if (opcode == ARP_OP_REQUEST) {
        if (!g_net_config.configured || ip_is_zero(g_net_config.ip)) {
            return;
        }

        if (memcmp(arp->target_ip, g_net_config.ip, 4) != 0) {
            return;
        }

        if (!valid_sender) {
            return;
        }

        arp_send_reply(arp->sender_mac, arp->sender_ip);
    }
}
