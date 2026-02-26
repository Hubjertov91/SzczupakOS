bool net_init(void) {
    memset(&g_net_config, 0, sizeof(g_net_config));
    memset(&g_dhcp, 0, sizeof(g_dhcp));
    memset(&g_ping_state, 0, sizeof(g_ping_state));
    memset(&g_dns_state, 0, sizeof(g_dns_state));
    tcp_runtime_reset();
    memset(&g_net_stats, 0, sizeof(g_net_stats));
    arp_cache_clear();
    dns_cache_clear();
    g_ping_sequence = 1;
    g_dns_txid = 1;
    g_dns_src_port = DNS_MIN_SRC_PORT;
    g_tcp_src_port = TCP_MIN_SRC_PORT;
    g_tcp_probe_seq_seed = 0x535A7001u;

    if (!rtl8139_init()) {
        return false;
    }

    return true;
}

void net_poll(void) {
    if (!g_rtl8139.initialized) {
        return;
    }

    uint32_t expected = 0;
    if (!__atomic_compare_exchange_n(&g_net_poll_active, &expected, 1, false,
                                     __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
        return;
    }

    for (uint8_t i = 0; i < 8u; i++) {
        size_t frame_length = 0;
        if (!rtl8139_poll_frame(g_rx_frame, sizeof(g_rx_frame), &frame_length)) {
            break;
        }
        if (frame_length >= sizeof(eth_header_t)) {
            g_net_stats.rx_frames++;
            net_handle_ethernet(g_rx_frame, frame_length);
        }
    }

    __atomic_store_n(&g_net_poll_active, 0, __ATOMIC_RELEASE);
}

bool net_configure_dhcp(uint32_t timeout_ms) {
    if (!g_rtl8139.initialized) {
        return false;
    }

    if (timeout_ms == 0) {
        timeout_ms = 5000;
    }

    uint32_t pit_frequency = pit_get_frequency();
    if (pit_frequency == 0) {
        serial_write("[NET] DHCP requires running PIT timer\n");
        return false;
    }

    memset(&g_dhcp, 0, sizeof(g_dhcp));
    dns_cache_clear();
    tcp_runtime_reset();
    g_dhcp.state = DHCP_STATE_WAIT_OFFER;
    g_dhcp.xid = g_next_dhcp_xid++;

    if (!dhcp_send_discover()) {
        serial_write("[NET] Failed to send DHCP discover\n");
        return false;
    }

    serial_write("[NET] DHCP discover sent\n");

    uint64_t now = pit_get_ticks();
    uint64_t timeout_ticks = ((uint64_t)timeout_ms * pit_frequency + 999u) / 1000u;
    uint64_t deadline = now + timeout_ticks;
    uint64_t retry_at = now + pit_frequency;

    while (pit_get_ticks() <= deadline) {
        net_poll();

        if (g_dhcp.state == DHCP_STATE_OFFER_READY) {
            if (!dhcp_send_request()) {
                serial_write("[NET] Failed to send DHCP request\n");
                return false;
            }
            g_dhcp.state = DHCP_STATE_WAIT_ACK;
            retry_at = pit_get_ticks() + pit_frequency;
            serial_write("[NET] DHCP request sent\n");
        } else if (g_dhcp.state == DHCP_STATE_BOUND) {
            if (!ip_is_zero(g_net_config.gateway)) {
                uint8_t gw_mac[6];
                if (arp_resolve(g_net_config.gateway, gw_mac, 1500)) {
                    serial_write("[NET] Gateway ARP: ");
                    serial_write_ip(g_net_config.gateway);
                    serial_write(" -> ");
                    serial_write_mac(gw_mac);
                    serial_write("\n");
                } else {
                    serial_write("[NET] Gateway ARP probe timed out\n");
                }
            }
            return true;
        }

        now = pit_get_ticks();
        if (now >= retry_at) {
            if (g_dhcp.state == DHCP_STATE_WAIT_OFFER) {
                dhcp_send_discover();
            } else if (g_dhcp.state == DHCP_STATE_WAIT_ACK) {
                dhcp_send_request();
            }
            retry_at = now + pit_frequency;
        }

        __asm__ volatile("pause");
    }

    serial_write("[NET] DHCP timed out\n");
    return false;
}

bool net_is_ready(void) {
    return g_rtl8139.initialized;
}

const char* net_get_backend_name(void) {
    return net_driver_backend_name();
}

const net_config_t* net_get_config(void) {
    return &g_net_config;
}

bool net_configure_static(const uint8_t ip[4], const uint8_t netmask[4],
                          const uint8_t gateway[4], const uint8_t dns[4]) {
    if (!g_rtl8139.initialized || !ip || !netmask || !gateway || !dns) {
        return false;
    }

    if (ip_is_zero(ip) || ip_is_broadcast(ip)) {
        return false;
    }

    memcpy(g_net_config.ip, ip, 4);
    memcpy(g_net_config.netmask, netmask, 4);
    memcpy(g_net_config.gateway, gateway, 4);
    memcpy(g_net_config.dns, dns, 4);
    g_net_config.lease_time_seconds = 0;
    g_net_config.configured = true;

    memset(&g_dhcp, 0, sizeof(g_dhcp));
    g_dhcp.state = DHCP_STATE_IDLE;
    arp_cache_clear();
    dns_cache_clear();
    tcp_runtime_reset();

    serial_write("[NET] Static IPv4 configured: ");
    serial_write_ip(g_net_config.ip);
    serial_write(" (gw ");
    serial_write_ip(g_net_config.gateway);
    serial_write(", mask ");
    serial_write_ip(g_net_config.netmask);
    serial_write(")\n");

    return true;
}

bool net_get_info(net_info_t* out_info) {
    if (!out_info) {
        return false;
    }

    memset(out_info, 0, sizeof(*out_info));
    out_info->link_up = net_driver_link_up();
    out_info->configured = g_net_config.configured;

    if (g_rtl8139.initialized) {
        memcpy(out_info->mac, g_rtl8139.mac, 6);
    }

    memcpy(out_info->ip, g_net_config.ip, 4);
    memcpy(out_info->netmask, g_net_config.netmask, 4);
    memcpy(out_info->gateway, g_net_config.gateway, 4);
    memcpy(out_info->dns, g_net_config.dns, 4);
    out_info->lease_time_seconds = g_net_config.lease_time_seconds;

    return true;
}

bool net_get_stats(net_stats_t* out_stats) {
    if (!out_stats) {
        return false;
    }

    arp_cache_expire_stale();
    dns_cache_expire_stale();

    *out_stats = g_net_stats;
    out_stats->arp_cache_entries = 0;
    out_stats->dns_cache_entries = 0;

    for (uint8_t i = 0; i < ARP_CACHE_SIZE; i++) {
        if (g_arp_cache[i].valid) {
            out_stats->arp_cache_entries++;
        }
    }

    for (uint8_t i = 0; i < DNS_CACHE_SIZE; i++) {
        if (g_dns_cache[i].valid) {
            out_stats->dns_cache_entries++;
        }
    }

    return true;
}

static bool net_icmp_probe_ipv4(const uint8_t dst_ip[4], uint8_t ttl, bool accept_time_exceeded,
                                uint32_t timeout_ms, uint32_t* out_rtt_ms, uint8_t out_reply_ip[4],
                                uint8_t* out_reply_icmp_type, bool* out_reached_dst) {
    if (!dst_ip || !g_rtl8139.initialized || !g_net_config.configured) {
        return false;
    }

    if (ip_is_zero(dst_ip) || ip_is_broadcast(dst_ip) || ip_is_zero(g_net_config.ip)) {
        return false;
    }

    uint32_t frequency = pit_get_frequency();
    if (frequency == 0) {
        return false;
    }

    if (timeout_ms == 0) {
        timeout_ms = NET_DEFAULT_PING_TIMEOUT_MS;
    }

    if (ttl == 0) {
        ttl = 64;
    }

    if (out_rtt_ms) {
        *out_rtt_ms = 0;
    }
    if (out_reply_ip) {
        memset(out_reply_ip, 0, 4);
    }
    if (out_reply_icmp_type) {
        *out_reply_icmp_type = 0;
    }
    if (out_reached_dst) {
        *out_reached_dst = false;
    }

    if (ip_equal(dst_ip, g_net_config.ip)) {
        if (out_reply_ip) {
            memcpy(out_reply_ip, dst_ip, 4);
        }
        if (out_reply_icmp_type) {
            *out_reply_icmp_type = ICMP_TYPE_ECHO_REPLY;
        }
        if (out_reached_dst) {
            *out_reached_dst = true;
        }
        return true;
    }

    size_t icmp_length = sizeof(icmp_echo_header_t) + sizeof(uint64_t);
    uint8_t icmp_packet[sizeof(icmp_echo_header_t) + sizeof(uint64_t)];
    memset(icmp_packet, 0, sizeof(icmp_packet));

    uint16_t sequence = g_ping_sequence++;
    if (g_ping_sequence == 0) {
        g_ping_sequence = 1;
    }

    icmp_echo_header_t* icmp = (icmp_echo_header_t*)icmp_packet;
    icmp->type = ICMP_TYPE_ECHO_REQUEST;
    icmp->code = 0;
    icmp->checksum = 0;
    icmp->identifier = net_htons(NET_PING_IDENTIFIER);
    icmp->sequence = net_htons(sequence);

    uint64_t send_tick = pit_get_ticks();
    memcpy(icmp_packet + sizeof(icmp_echo_header_t), &send_tick, sizeof(send_tick));
    icmp->checksum = net_htons(icmp_checksum(icmp_packet, icmp_length));

    memset(&g_ping_state, 0, sizeof(g_ping_state));
    g_ping_state.waiting = true;
    g_ping_state.received = false;
    g_ping_state.identifier = NET_PING_IDENTIFIER;
    g_ping_state.sequence = sequence;
    g_ping_state.expected_src_ip = ip_to_u32(dst_ip);
    g_ping_state.accept_time_exceeded = accept_time_exceeded;
    g_ping_state.reply_src_ip = 0;
    g_ping_state.reply_icmp_type = 0;
    g_ping_state.sent_tick = send_tick;

    if (!net_send_ipv4_payload(g_net_config.ip, dst_ip, IP_PROTO_ICMP, ttl, icmp_packet, icmp_length, false)) {
        g_ping_state.waiting = false;
        return false;
    }
    g_net_stats.tx_icmp++;

    uint64_t timeout_ticks = ((uint64_t)timeout_ms * frequency + 999u) / 1000u;
    if (timeout_ticks == 0) {
        timeout_ticks = 1;
    }
    uint64_t deadline = send_tick + timeout_ticks;

    while (pit_get_ticks() <= deadline) {
        net_poll();
        if (g_ping_state.received) {
            uint64_t delta_ticks = g_ping_state.received_tick - g_ping_state.sent_tick;
            uint32_t rtt_ms = (uint32_t)((delta_ticks * 1000u) / frequency);
            if (out_rtt_ms) {
                *out_rtt_ms = rtt_ms;
            }

            uint8_t reply_ip[4];
            if (g_ping_state.reply_src_ip != 0) {
                ip_from_u32(g_ping_state.reply_src_ip, reply_ip);
            } else {
                memcpy(reply_ip, dst_ip, 4);
            }
            if (out_reply_ip) {
                memcpy(out_reply_ip, reply_ip, 4);
            }
            if (out_reply_icmp_type) {
                *out_reply_icmp_type = g_ping_state.reply_icmp_type;
            }
            if (out_reached_dst) {
                *out_reached_dst = (g_ping_state.reply_icmp_type == ICMP_TYPE_ECHO_REPLY &&
                                    g_ping_state.reply_src_ip == g_ping_state.expected_src_ip);
            }

            g_ping_state.waiting = false;
            return true;
        }
        __asm__ volatile("pause");
    }

    g_ping_state.waiting = false;
    return false;
}

bool net_ping_ipv4(const uint8_t dst_ip[4], uint32_t timeout_ms, uint32_t* out_rtt_ms) {
    return net_icmp_probe_ipv4(dst_ip, 64, false, timeout_ms, out_rtt_ms, NULL, NULL, NULL);
}

bool net_trace_probe_ipv4(const uint8_t dst_ip[4], uint8_t ttl, uint32_t timeout_ms,
                          uint32_t* out_rtt_ms, uint8_t out_hop_ip[4], bool* out_reached_dst) {
    uint8_t reply_ip[4] = {0, 0, 0, 0};
    bool reached_dst = false;
    uint8_t probe_ttl = (ttl == 0) ? 1u : ttl;

    bool ok = net_icmp_probe_ipv4(dst_ip, probe_ttl, true, timeout_ms, out_rtt_ms,
                                  reply_ip, NULL, &reached_dst);
    if (out_hop_ip) {
        if (ok) {
            memcpy(out_hop_ip, reply_ip, 4);
        } else {
            memset(out_hop_ip, 0, 4);
        }
    }
    if (out_reached_dst) {
        *out_reached_dst = ok && reached_dst;
    }
    return ok;
}

bool net_tcp_probe_ipv4(const uint8_t dst_ip[4], uint16_t dst_port, uint32_t timeout_ms,
                        bool* out_open, uint32_t* out_rtt_ms) {
    if (!dst_ip || dst_port == 0 || !g_rtl8139.initialized || !g_net_config.configured) {
        return false;
    }

    if (ip_is_zero(dst_ip) || ip_is_broadcast(dst_ip) || ip_is_zero(g_net_config.ip)) {
        return false;
    }

    uint32_t frequency = pit_get_frequency();
    if (frequency == 0) {
        return false;
    }

    if (timeout_ms == 0) {
        timeout_ms = NET_DEFAULT_TCP_TIMEOUT_MS;
    }

    if (out_open) {
        *out_open = false;
    }
    if (out_rtt_ms) {
        *out_rtt_ms = 0;
    }

    uint16_t src_port = g_tcp_src_port++;
    if (g_tcp_src_port < TCP_MIN_SRC_PORT) {
        g_tcp_src_port = TCP_MIN_SRC_PORT;
    }
    if (src_port == 0) {
        src_port = TCP_MIN_SRC_PORT;
    }

    uint32_t send_tick32 = (uint32_t)pit_get_ticks();
    uint32_t sequence = g_tcp_probe_seq_seed ^ (send_tick32 << 5) ^ ((uint32_t)src_port << 16) ^ (uint32_t)dst_port;
    g_tcp_probe_seq_seed += 0x01010101u;

    memset(&g_tcp_probe_state, 0, sizeof(g_tcp_probe_state));
    g_tcp_probe_state.waiting = true;
    g_tcp_probe_state.src_port = src_port;
    g_tcp_probe_state.dst_port = dst_port;
    g_tcp_probe_state.expected_src_ip = ip_to_u32(dst_ip);
    g_tcp_probe_state.sequence = sequence;
    g_tcp_probe_state.sent_tick = pit_get_ticks();

    if (!net_send_tcp_ipv4(g_net_config.ip, dst_ip, src_port, dst_port,
                           sequence, 0u, TCP_FLAG_SYN, 64u, NULL, 0u)) {
        g_tcp_probe_state.waiting = false;
        return false;
    }

    uint64_t timeout_ticks = ((uint64_t)timeout_ms * frequency + 999u) / 1000u;
    if (timeout_ticks == 0) {
        timeout_ticks = 1;
    }
    uint64_t deadline = g_tcp_probe_state.sent_tick + timeout_ticks;

    while (pit_get_ticks() <= deadline) {
        net_poll();
        if (g_tcp_probe_state.received) {
            uint64_t delta_ticks = g_tcp_probe_state.received_tick - g_tcp_probe_state.sent_tick;
            uint32_t rtt_ms = (uint32_t)((delta_ticks * 1000u) / frequency);
            if (out_rtt_ms) {
                *out_rtt_ms = rtt_ms;
            }
            if (out_open) {
                *out_open = g_tcp_probe_state.open;
            }
            g_tcp_probe_state.waiting = false;
            return true;
        }
        __asm__ volatile("pause");
    }

    g_tcp_probe_state.waiting = false;
    return false;
}

bool net_dns_resolve_ipv4(const char* hostname, uint8_t out_ip[4], uint32_t timeout_ms) {
    if (!hostname || !out_ip || !g_rtl8139.initialized || !g_net_config.configured) {
        return false;
    }

    if (ip_is_zero(g_net_config.ip) || ip_is_zero(g_net_config.dns)) {
        return false;
    }

    uint32_t frequency = pit_get_frequency();
    if (frequency == 0) {
        return false;
    }

    if (timeout_ms == 0) {
        timeout_ms = DNS_DEFAULT_TIMEOUT_MS;
    }

    if (!dns_validate_hostname(hostname)) {
        return false;
    }

    if (dns_cache_lookup(hostname, out_ip)) {
        return true;
    }

    memset(g_dns_query_packet, 0, sizeof(g_dns_query_packet));

    uint16_t txid = g_dns_txid++;
    if (g_dns_txid == 0) {
        g_dns_txid = 1;
    }

    uint16_t src_port = g_dns_src_port++;
    if (g_dns_src_port < DNS_MIN_SRC_PORT) {
        g_dns_src_port = DNS_MIN_SRC_PORT;
    }

    write_be16(&g_dns_query_packet[0], txid);
    write_be16(&g_dns_query_packet[2], 0x0100u);
    write_be16(&g_dns_query_packet[4], 1u);
    write_be16(&g_dns_query_packet[6], 0u);
    write_be16(&g_dns_query_packet[8], 0u);
    write_be16(&g_dns_query_packet[10], 0u);

    size_t query_offset = 12u;
    size_t qname_len = 0;
    if (!dns_encode_qname(hostname, &g_dns_query_packet[query_offset], sizeof(g_dns_query_packet) - query_offset, &qname_len)) {
        return false;
    }
    query_offset += qname_len;

    if (query_offset + 4u > sizeof(g_dns_query_packet)) {
        return false;
    }
    write_be16(&g_dns_query_packet[query_offset], 1u);
    query_offset += 2u;
    write_be16(&g_dns_query_packet[query_offset], 1u);
    query_offset += 2u;

    memset(&g_dns_state, 0, sizeof(g_dns_state));
    g_dns_state.waiting = true;
    g_dns_state.txid = txid;
    g_dns_state.src_port = src_port;
    g_dns_state.expected_server_ip = ip_to_u32(g_net_config.dns);
    g_dns_state.ttl_seconds = DNS_MIN_TTL_S;
    g_dns_state.sent_tick = pit_get_ticks();
    g_net_stats.dns_queries++;

    if (!net_send_udp_ipv4(g_net_config.ip, g_net_config.dns, src_port, DNS_SERVER_PORT,
                           g_dns_query_packet, query_offset, false)) {
        g_dns_state.waiting = false;
        return false;
    }

    uint64_t timeout_ticks = ((uint64_t)timeout_ms * frequency + 999u) / 1000u;
    if (timeout_ticks == 0) {
        timeout_ticks = 1;
    }
    uint64_t retry_ticks = ((uint64_t)DNS_DEFAULT_RETRY_MS * frequency + 999u) / 1000u;
    if (retry_ticks == 0) {
        retry_ticks = 1;
    }

    uint64_t deadline = g_dns_state.sent_tick + timeout_ticks;
    uint64_t retry_at = g_dns_state.sent_tick + retry_ticks;

    while (pit_get_ticks() <= deadline) {
        net_poll();

        if (g_dns_state.received) {
            memcpy(out_ip, g_dns_state.resolved_ip, 4);
            dns_cache_update(hostname, g_dns_state.resolved_ip, g_dns_state.ttl_seconds);
            g_dns_state.waiting = false;
            return true;
        }

        uint64_t now = pit_get_ticks();
        if (now >= retry_at) {
            net_send_udp_ipv4(g_net_config.ip, g_net_config.dns, src_port, DNS_SERVER_PORT,
                              g_dns_query_packet, query_offset, false);
            retry_at = now + retry_ticks;
        }

        __asm__ volatile("pause");
    }

    g_dns_state.waiting = false;
    g_net_stats.dns_timeouts++;
    return false;
}
