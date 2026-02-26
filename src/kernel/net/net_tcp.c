typedef struct {
    bool active;
    bool established;
    bool finished;
    bool reset;
    bool truncated;
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t remote_ip;
    uint32_t snd_nxt;
    uint32_t rcv_nxt;
    uint8_t* rx_buffer;
    uint32_t rx_capacity;
    uint32_t rx_length;
    uint64_t last_activity_tick;
} tcp_client_state_t;

static tcp_client_state_t g_tcp_client;

static uint32_t tcp_segment_advance(uint8_t flags, size_t payload_length) {
    uint32_t advance = (uint32_t)payload_length;
    if ((flags & TCP_FLAG_SYN) != 0) {
        advance++;
    }
    if ((flags & TCP_FLAG_FIN) != 0) {
        advance++;
    }
    return advance;
}

static void tcp_runtime_reset(void) {
    memset(&g_tcp_probe_state, 0, sizeof(g_tcp_probe_state));
    memset(&g_tcp_client, 0, sizeof(g_tcp_client));
}

static bool tcp_client_matches(uint32_t src_ip, uint16_t src_port, uint16_t dst_port) {
    return g_tcp_client.active &&
           src_ip == g_tcp_client.remote_ip &&
           src_port == g_tcp_client.dst_port &&
           dst_port == g_tcp_client.src_port;
}

static bool tcp_client_send_segment(uint8_t flags, const uint8_t* payload, size_t payload_length) {
    if (!g_tcp_client.active || !g_net_config.configured || ip_is_zero(g_net_config.ip)) {
        return false;
    }

    uint8_t dst_ip[4];
    ip_from_u32(g_tcp_client.remote_ip, dst_ip);

    uint32_t ack_number = ((flags & TCP_FLAG_ACK) != 0) ? g_tcp_client.rcv_nxt : 0u;
    uint32_t sequence = g_tcp_client.snd_nxt;

    if (!net_send_tcp_ipv4(g_net_config.ip, dst_ip,
                           g_tcp_client.src_port, g_tcp_client.dst_port,
                           sequence, ack_number, flags, 64u,
                           payload, payload_length)) {
        return false;
    }

    g_tcp_client.snd_nxt += tcp_segment_advance(flags, payload_length);
    return true;
}

static void tcp_client_abort(void) {
    if (!g_tcp_client.active) {
        return;
    }

    if (g_tcp_client.established && !g_tcp_client.reset) {
        uint8_t dst_ip[4];
        ip_from_u32(g_tcp_client.remote_ip, dst_ip);
        (void)net_send_tcp_ipv4(g_net_config.ip, dst_ip,
                                g_tcp_client.src_port, g_tcp_client.dst_port,
                                g_tcp_client.snd_nxt, g_tcp_client.rcv_nxt,
                                (uint8_t)(TCP_FLAG_RST | TCP_FLAG_ACK), 64u,
                                NULL, 0u);
    }

    g_tcp_client.active = false;
}

static void tcp_send_reset_for_segment(uint32_t src_ip, uint32_t dst_ip,
                                       uint16_t src_port, uint16_t dst_port,
                                       uint32_t sequence, uint32_t ack_number,
                                       uint8_t rx_flags, size_t payload_length) {
    if ((rx_flags & TCP_FLAG_RST) != 0) {
        return;
    }

    uint8_t src_ip_bytes[4];
    uint8_t dst_ip_bytes[4];
    ip_from_u32(src_ip, src_ip_bytes);
    ip_from_u32(dst_ip, dst_ip_bytes);
    if (ip_is_zero(dst_ip_bytes)) {
        return;
    }

    if ((rx_flags & TCP_FLAG_ACK) != 0) {
        (void)net_send_tcp_ipv4(dst_ip_bytes, src_ip_bytes, dst_port, src_port,
                                ack_number, 0u, TCP_FLAG_RST, 64u,
                                NULL, 0u);
        return;
    }

    uint32_t reset_ack = sequence + tcp_segment_advance(rx_flags, payload_length);
    (void)net_send_tcp_ipv4(dst_ip_bytes, src_ip_bytes, dst_port, src_port,
                            0u, reset_ack, (uint8_t)(TCP_FLAG_RST | TCP_FLAG_ACK), 64u,
                            NULL, 0u);
}

static bool tcp_client_handle_segment(uint32_t src_ip,
                                      uint16_t src_port,
                                      uint16_t dst_port,
                                      uint32_t sequence,
                                      uint32_t ack_number,
                                      uint8_t flags,
                                      const uint8_t* segment_payload,
                                      size_t payload_length) {
    if (!tcp_client_matches(src_ip, src_port, dst_port)) {
        return false;
    }

    g_tcp_client.last_activity_tick = pit_get_ticks();

    if ((flags & TCP_FLAG_RST) != 0) {
        g_tcp_client.reset = true;
        g_tcp_client.finished = true;
        return true;
    }

    if (!g_tcp_client.established) {
        if ((flags & (TCP_FLAG_SYN | TCP_FLAG_ACK)) == (TCP_FLAG_SYN | TCP_FLAG_ACK) &&
            ack_number == g_tcp_client.snd_nxt) {
            g_tcp_client.established = true;
            g_tcp_client.rcv_nxt = sequence + 1u;
            (void)tcp_client_send_segment(TCP_FLAG_ACK, NULL, 0u);
        }
        return true;
    }

    if (sequence != g_tcp_client.rcv_nxt) {
        if (payload_length > 0 || (flags & (TCP_FLAG_FIN | TCP_FLAG_SYN)) != 0) {
            (void)tcp_client_send_segment(TCP_FLAG_ACK, NULL, 0u);
        }
        return true;
    }

    if (payload_length > 0 && segment_payload) {
        uint32_t copy_len = (uint32_t)payload_length;
        uint32_t remaining = (g_tcp_client.rx_length < g_tcp_client.rx_capacity)
                                 ? (g_tcp_client.rx_capacity - g_tcp_client.rx_length)
                                 : 0u;

        if (copy_len > remaining) {
            copy_len = remaining;
            g_tcp_client.truncated = true;
        }

        if (copy_len > 0) {
            memcpy(&g_tcp_client.rx_buffer[g_tcp_client.rx_length], segment_payload, copy_len);
            g_tcp_client.rx_length += copy_len;
        }

        g_tcp_client.rcv_nxt += (uint32_t)payload_length;
    }

    bool fin = (flags & TCP_FLAG_FIN) != 0;
    if (fin) {
        g_tcp_client.rcv_nxt++;
        g_tcp_client.finished = true;
    }

    if (payload_length > 0 || fin) {
        (void)tcp_client_send_segment(TCP_FLAG_ACK, NULL, 0u);
    }

    return true;
}

static bool tcp_append_bytes(char* dst, size_t capacity, size_t* inout_pos,
                             const char* src, size_t src_len) {
    if (!dst || !inout_pos || (!src && src_len != 0)) {
        return false;
    }

    size_t pos = *inout_pos;
    if (pos + src_len > capacity) {
        return false;
    }

    if (src_len > 0) {
        memcpy(dst + pos, src, src_len);
    }
    *inout_pos = pos + src_len;
    return true;
}

static bool tcp_append_cstr(char* dst, size_t capacity, size_t* inout_pos, const char* src) {
    if (!src) {
        return false;
    }
    return tcp_append_bytes(dst, capacity, inout_pos, src, strlen(src));
}

static bool tcp_append_u16(char* dst, size_t capacity, size_t* inout_pos, uint16_t value) {
    char tmp[6];
    size_t len = 0;

    if (value == 0) {
        tmp[len++] = '0';
    } else {
        while (value > 0 && len < sizeof(tmp)) {
            tmp[len++] = (char)('0' + (value % 10u));
            value = (uint16_t)(value / 10u);
        }
    }

    if (len == 0) {
        return false;
    }

    for (size_t i = 0; i < len / 2u; i++) {
        char c = tmp[i];
        tmp[i] = tmp[len - 1u - i];
        tmp[len - 1u - i] = c;
    }

    return tcp_append_bytes(dst, capacity, inout_pos, tmp, len);
}

static bool tcp_http_build_request(char* out, size_t capacity, size_t* out_length,
                                   const char* host, const char* path, uint16_t port) {
    if (!out || !out_length || !host || host[0] == '\0') {
        return false;
    }

    const char* req_path = (path && path[0]) ? path : "/";
    if (req_path[0] != '/') {
        return false;
    }

    size_t host_len = strlen(host);
    size_t path_len = strlen(req_path);
    if (host_len > 200u || path_len > 300u) {
        return false;
    }

    size_t pos = 0;
    if (!tcp_append_cstr(out, capacity, &pos, "GET ")) return false;
    if (!tcp_append_cstr(out, capacity, &pos, req_path)) return false;
    if (!tcp_append_cstr(out, capacity, &pos, " HTTP/1.1\r\nHost: ")) return false;
    if (!tcp_append_cstr(out, capacity, &pos, host)) return false;
    if (port != 80u) {
        if (!tcp_append_cstr(out, capacity, &pos, ":")) return false;
        if (!tcp_append_u16(out, capacity, &pos, port)) return false;
    }
    if (!tcp_append_cstr(out, capacity, &pos,
                         "\r\nUser-Agent: SzczupakOS/0.1\r\nAccept: */*\r\nConnection: close\r\n\r\n")) {
        return false;
    }

    *out_length = pos;
    return true;
}

static bool http_find_headers_end(const uint8_t* data, uint32_t length, uint32_t* out_offset) {
    if (!data || !out_offset || length < 4u) {
        return false;
    }

    for (uint32_t i = 0; i + 3u < length; i++) {
        if (data[i] == '\r' && data[i + 1u] == '\n' &&
            data[i + 2u] == '\r' && data[i + 3u] == '\n') {
            *out_offset = i + 4u;
            return true;
        }
    }

    return false;
}

static bool http_parse_status_code(const uint8_t* data, uint32_t length, uint16_t* out_status_code) {
    if (!data || !out_status_code || length < 12u) {
        return false;
    }

    if (memcmp(data, "HTTP/", 5u) != 0) {
        return false;
    }

    uint32_t i = 5u;
    while (i < length && data[i] != ' ' && data[i] != '\r' && data[i] != '\n') {
        i++;
    }

    if (i + 4u > length || data[i] != ' ') {
        return false;
    }

    uint8_t c0 = data[i + 1u];
    uint8_t c1 = data[i + 2u];
    uint8_t c2 = data[i + 3u];
    if (c0 < '0' || c0 > '9' || c1 < '0' || c1 > '9' || c2 < '0' || c2 > '9') {
        return false;
    }

    *out_status_code = (uint16_t)(((c0 - '0') * 100u) + ((c1 - '0') * 10u) + (c2 - '0'));
    return true;
}

bool net_http_get_ipv4(const uint8_t dst_ip[4], uint16_t dst_port,
                       const char* host, const char* path,
                       uint32_t timeout_ms,
                       uint8_t* out_body, uint32_t out_body_capacity,
                       uint32_t* out_body_length,
                       uint16_t* out_status_code,
                       bool* out_truncated) {
    if (out_body_length) {
        *out_body_length = 0;
    }
    if (out_status_code) {
        *out_status_code = 0;
    }
    if (out_truncated) {
        *out_truncated = false;
    }

    if (!dst_ip || !host || !out_body || out_body_capacity == 0u ||
        !g_rtl8139.initialized || !g_net_config.configured) {
        return false;
    }

    if (dst_port == 0u) {
        dst_port = 80u;
    }

    if (timeout_ms == 0u) {
        timeout_ms = 5000u;
    }

    if (ip_is_zero(dst_ip) || ip_is_broadcast(dst_ip) || ip_is_zero(g_net_config.ip)) {
        return false;
    }

    uint32_t frequency = pit_get_frequency();
    if (frequency == 0u) {
        return false;
    }

    size_t request_length = 0;
    char request[700];
    if (!tcp_http_build_request(request, sizeof(request), &request_length, host, path, dst_port)) {
        return false;
    }

    uint64_t timeout_ticks = ((uint64_t)timeout_ms * frequency + 999u) / 1000u;
    if (timeout_ticks == 0u) {
        timeout_ticks = 1u;
    }

    uint64_t idle_ticks = ((uint64_t)900u * frequency + 999u) / 1000u;
    if (idle_ticks == 0u) {
        idle_ticks = 1u;
    }

    uint16_t src_port = g_tcp_src_port++;
    if (g_tcp_src_port < TCP_MIN_SRC_PORT) {
        g_tcp_src_port = TCP_MIN_SRC_PORT;
    }
    if (src_port == 0u) {
        src_port = TCP_MIN_SRC_PORT;
    }

    uint32_t send_tick32 = (uint32_t)pit_get_ticks();
    uint32_t sequence = g_tcp_probe_seq_seed ^ (send_tick32 << 3) ^ ((uint32_t)src_port << 16) ^ (uint32_t)dst_port;
    g_tcp_probe_seq_seed += 0x01010101u;

    memset(&g_tcp_client, 0, sizeof(g_tcp_client));
    g_tcp_client.active = true;
    g_tcp_client.src_port = src_port;
    g_tcp_client.dst_port = dst_port;
    g_tcp_client.remote_ip = ip_to_u32(dst_ip);
    g_tcp_client.snd_nxt = sequence;
    g_tcp_client.rcv_nxt = 0u;
    g_tcp_client.rx_buffer = out_body;
    g_tcp_client.rx_capacity = out_body_capacity;
    g_tcp_client.rx_length = 0u;
    g_tcp_client.last_activity_tick = pit_get_ticks();

    if (!tcp_client_send_segment(TCP_FLAG_SYN, NULL, 0u)) {
        tcp_client_abort();
        memset(&g_tcp_client, 0, sizeof(g_tcp_client));
        return false;
    }

    uint64_t deadline = pit_get_ticks() + timeout_ticks;
    while (pit_get_ticks() <= deadline) {
        net_poll();
        if (g_tcp_client.reset) {
            tcp_client_abort();
            memset(&g_tcp_client, 0, sizeof(g_tcp_client));
            return false;
        }
        if (g_tcp_client.established) {
            break;
        }
        __asm__ volatile("pause");
    }

    if (!g_tcp_client.established) {
        tcp_client_abort();
        memset(&g_tcp_client, 0, sizeof(g_tcp_client));
        return false;
    }

    if (!tcp_client_send_segment((uint8_t)(TCP_FLAG_ACK | TCP_FLAG_PSH),
                                 (const uint8_t*)request, request_length)) {
        tcp_client_abort();
        memset(&g_tcp_client, 0, sizeof(g_tcp_client));
        return false;
    }

    uint32_t last_rx_length = g_tcp_client.rx_length;
    uint64_t absolute_deadline = pit_get_ticks() + timeout_ticks;
    uint64_t idle_deadline = pit_get_ticks() + idle_ticks;

    while (pit_get_ticks() <= absolute_deadline) {
        net_poll();

        if (g_tcp_client.reset) {
            break;
        }

        if (g_tcp_client.rx_length != last_rx_length) {
            last_rx_length = g_tcp_client.rx_length;
            idle_deadline = pit_get_ticks() + idle_ticks;
        }

        if (g_tcp_client.finished) {
            break;
        }

        if (g_tcp_client.rx_length > 0u && pit_get_ticks() >= idle_deadline) {
            break;
        }

        __asm__ volatile("pause");
    }

    uint32_t response_len = g_tcp_client.rx_length;
    bool response_truncated = g_tcp_client.truncated;
    bool response_reset = g_tcp_client.reset;

    uint16_t status_code = 0;
    (void)http_parse_status_code(out_body, response_len, &status_code);

    uint32_t body_len = response_len;
    uint32_t body_offset = 0u;
    if (http_find_headers_end(out_body, response_len, &body_offset) && body_offset <= response_len) {
        body_len = response_len - body_offset;
        if (body_offset > 0u && body_len > 0u) {
            memmove(out_body, out_body + body_offset, body_len);
        }
    }

    if (out_body_length) {
        *out_body_length = body_len;
    }
    if (out_status_code) {
        *out_status_code = status_code;
    }
    if (out_truncated) {
        *out_truncated = response_truncated;
    }

    tcp_client_abort();
    memset(&g_tcp_client, 0, sizeof(g_tcp_client));

    if (response_reset) {
        return false;
    }

    return response_len > 0u;
}

static void net_handle_tcp(const uint8_t* payload, size_t length, uint32_t src_ip, uint32_t dst_ip) {
    if (!payload || length < sizeof(tcp_header_t) || length > 0xFFFFu) {
        return;
    }

    const tcp_header_t* tcp = (const tcp_header_t*)payload;
    uint8_t data_offset_words = (uint8_t)(tcp->data_offset_reserved >> 4);
    size_t header_length = (size_t)data_offset_words * 4u;
    if (header_length < sizeof(tcp_header_t) || header_length > length) {
        return;
    }

    uint16_t verify = tcp_checksum(src_ip, dst_ip, payload, (uint16_t)length);
    if (verify != 0u && verify != 0xFFFFu) {
        return;
    }

    g_net_stats.rx_tcp++;

    uint16_t src_port = net_ntohs(tcp->src_port);
    uint16_t dst_port = net_ntohs(tcp->dst_port);
    uint8_t flags = tcp->flags;
    uint32_t sequence = net_ntohl(tcp->sequence_number);
    uint32_t ack_number = net_ntohl(tcp->ack_number);
    const uint8_t* segment_payload = payload + header_length;
    size_t payload_length = length - header_length;

    if (src_port == 0 || dst_port == 0) {
        return;
    }

    if (g_tcp_probe_state.waiting &&
        !g_tcp_probe_state.received &&
        src_ip == g_tcp_probe_state.expected_src_ip &&
        src_port == g_tcp_probe_state.dst_port &&
        dst_port == g_tcp_probe_state.src_port) {
        if ((flags & TCP_FLAG_RST) != 0) {
            g_tcp_probe_state.received = true;
            g_tcp_probe_state.open = false;
            g_tcp_probe_state.received_tick = pit_get_ticks();
            return;
        }

        if ((flags & (TCP_FLAG_SYN | TCP_FLAG_ACK)) == (TCP_FLAG_SYN | TCP_FLAG_ACK) &&
            ack_number == (g_tcp_probe_state.sequence + 1u)) {
            g_tcp_probe_state.received = true;
            g_tcp_probe_state.open = true;
            g_tcp_probe_state.received_tick = pit_get_ticks();

            uint8_t src_ip_bytes[4];
            ip_from_u32(src_ip, src_ip_bytes);
            (void)net_send_tcp_ipv4(g_net_config.ip, src_ip_bytes,
                                    g_tcp_probe_state.src_port, g_tcp_probe_state.dst_port,
                                    g_tcp_probe_state.sequence + 1u, sequence + 1u,
                                    (uint8_t)(TCP_FLAG_RST | TCP_FLAG_ACK), 64u,
                                    NULL, 0u);
            return;
        }
    }

    if (tcp_client_handle_segment(src_ip, src_port, dst_port,
                                  sequence, ack_number, flags,
                                  segment_payload, payload_length)) {
        return;
    }

    uint8_t dst_ip_bytes[4];
    ip_from_u32(dst_ip, dst_ip_bytes);
    if (!g_net_config.configured || ip_is_zero(g_net_config.ip) || !ip_equal(dst_ip_bytes, g_net_config.ip)) {
        return;
    }

    tcp_send_reset_for_segment(src_ip, dst_ip, src_port, dst_port,
                               sequence, ack_number, flags, payload_length);
}
