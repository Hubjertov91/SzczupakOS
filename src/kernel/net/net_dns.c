static bool dns_validate_hostname(const char* hostname) {
    if (!hostname) {
        return false;
    }

    size_t total_len = 0;
    size_t label_len = 0;

    for (size_t i = 0;; i++) {
        char c = hostname[i];

        if (c == '\0') {
            if (total_len == 0 || label_len == 0) {
                return false;
            }
            if (label_len > 63) {
                return false;
            }
            return total_len <= DNS_MAX_HOSTNAME_LEN;
        }

        if (c == '.') {
            if (label_len == 0 || label_len > 63) {
                return false;
            }
            label_len = 0;
        } else {
            bool alpha = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
            bool digit = (c >= '0' && c <= '9');
            if (!(alpha || digit || c == '-')) {
                return false;
            }
            label_len++;
            if (label_len > 63) {
                return false;
            }
        }

        total_len++;
        if (total_len > DNS_MAX_HOSTNAME_LEN) {
            return false;
        }
    }
}

static bool dns_encode_qname(const char* hostname, uint8_t* out, size_t out_capacity, size_t* out_length) {
    if (!hostname || !out || !out_length) {
        return false;
    }
    if (!dns_validate_hostname(hostname)) {
        return false;
    }

    size_t out_pos = 0;
    size_t label_start = 0;
    size_t host_len = strlen(hostname);

    for (size_t i = 0; i <= host_len; i++) {
        bool at_separator = (hostname[i] == '.') || (hostname[i] == '\0');
        if (!at_separator) {
            continue;
        }

        size_t label_len = i - label_start;
        if (label_len == 0 || label_len > 63) {
            return false;
        }
        if (out_pos + 1 + label_len >= out_capacity) {
            return false;
        }

        out[out_pos++] = (uint8_t)label_len;
        memcpy(&out[out_pos], &hostname[label_start], label_len);
        out_pos += label_len;

        label_start = i + 1;
    }

    if (out_pos >= out_capacity) {
        return false;
    }
    out[out_pos++] = 0;
    *out_length = out_pos;
    return true;
}

static bool dns_skip_name(const uint8_t* packet, size_t packet_length, size_t* inout_offset) {
    if (!packet || !inout_offset) {
        return false;
    }

    size_t offset = *inout_offset;
    size_t labels = 0;

    while (offset < packet_length) {
        uint8_t len = packet[offset];
        if (len == 0) {
            offset++;
            *inout_offset = offset;
            return true;
        }

        if ((len & 0xC0u) == 0xC0u) {
            if (offset + 1 >= packet_length) {
                return false;
            }
            offset += 2;
            *inout_offset = offset;
            return true;
        }

        if ((len & 0xC0u) != 0 || len > 63u) {
            return false;
        }

        offset++;
        if (offset + len > packet_length) {
            return false;
        }
        offset += len;

        labels++;
        if (labels > 128u) {
            return false;
        }
    }

    return false;
}

static bool dns_parse_response_a(const uint8_t* packet, size_t packet_length, uint16_t expected_txid,
                                 uint8_t out_ip[4], uint32_t* out_ttl_seconds) {
    if (!packet || !out_ip || packet_length < 12u) {
        return false;
    }

    uint16_t txid = read_be16(&packet[0]);
    uint16_t flags = read_be16(&packet[2]);
    uint16_t qdcount = read_be16(&packet[4]);
    uint16_t ancount = read_be16(&packet[6]);

    if (txid != expected_txid) {
        return false;
    }

    if ((flags & 0x8000u) == 0) {
        return false;
    }

    if ((flags & 0x000Fu) != 0) {
        return false;
    }

    size_t offset = 12u;

    for (uint16_t i = 0; i < qdcount; i++) {
        if (!dns_skip_name(packet, packet_length, &offset)) {
            return false;
        }
        if (offset + 4u > packet_length) {
            return false;
        }
        offset += 4u;
    }

    for (uint16_t i = 0; i < ancount; i++) {
        if (!dns_skip_name(packet, packet_length, &offset)) {
            return false;
        }
        if (offset + 10u > packet_length) {
            return false;
        }

        uint16_t type = read_be16(&packet[offset + 0]);
        uint16_t class_code = read_be16(&packet[offset + 2]);
        uint32_t ttl = read_be32(&packet[offset + 4]);
        uint16_t rdlength = read_be16(&packet[offset + 8]);
        offset += 10u;

        if (offset + rdlength > packet_length) {
            return false;
        }

        if (type == 1u && class_code == 1u && rdlength == 4u) {
            memcpy(out_ip, &packet[offset], 4u);
            if (out_ttl_seconds) {
                *out_ttl_seconds = ttl;
            }
            return true;
        }

        offset += rdlength;
    }

    return false;
}

static void dns_handle_udp_packet(const uint8_t* payload, size_t length,
                                  uint32_t src_ip, uint16_t src_port, uint16_t dst_port) {
    if (!payload || !g_dns_state.waiting || g_dns_state.received) {
        return;
    }

    if (src_port != DNS_SERVER_PORT || dst_port != g_dns_state.src_port) {
        return;
    }

    if (src_ip != g_dns_state.expected_server_ip) {
        return;
    }

    uint8_t resolved_ip[4];
    uint32_t ttl_seconds = DNS_MIN_TTL_S;
    if (!dns_parse_response_a(payload, length, g_dns_state.txid, resolved_ip, &ttl_seconds)) {
        return;
    }

    memcpy(g_dns_state.resolved_ip, resolved_ip, 4);
    g_dns_state.ttl_seconds = dns_normalize_ttl(ttl_seconds);
    g_dns_state.received = true;
    g_dns_state.received_tick = pit_get_ticks();
}

static void net_handle_udp(const uint8_t* payload, size_t length, uint32_t src_ip, uint32_t dst_ip) {
    if (!payload || length < sizeof(udp_header_t)) {
        return;
    }

    const udp_header_t* udp = (const udp_header_t*)payload;
    uint16_t src_port = net_ntohs(udp->src_port);
    uint16_t dst_port = net_ntohs(udp->dst_port);
    uint16_t udp_length = net_ntohs(udp->length);

    if (udp_length < sizeof(udp_header_t) || udp_length > length) {
        return;
    }

    const uint8_t* udp_payload = payload + sizeof(udp_header_t);
    size_t udp_payload_length = (size_t)udp_length - sizeof(udp_header_t);

    if (udp->checksum != 0) {
        if (udp_length <= sizeof(g_udp_verify_packet)) {
            memcpy(g_udp_verify_packet, payload, udp_length);
            ((udp_header_t*)g_udp_verify_packet)->checksum = udp->checksum;
            uint16_t verify = udp_checksum(src_ip, dst_ip, g_udp_verify_packet, udp_length);
            if (verify != 0xFFFFu && verify != 0u) {
                return;
            }
        }
    }

    g_net_stats.rx_udp++;

    if (src_port == DHCP_SERVER_PORT && dst_port == DHCP_CLIENT_PORT) {
        g_net_stats.rx_dhcp++;
        dhcp_handle_packet(udp_payload, udp_payload_length, src_ip);
        return;
    }

    if (src_port == DNS_SERVER_PORT) {
        g_net_stats.rx_dns++;
    }
    dns_handle_udp_packet(udp_payload, udp_payload_length, src_ip, src_port, dst_port);
}
