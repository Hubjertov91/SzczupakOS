static void dhcp_options_clear(dhcp_options_t* options) {
    if (!options) return;
    memset(options, 0, sizeof(*options));
}

static void dhcp_parse_options(const uint8_t* options_data, size_t options_length, dhcp_options_t* out) {
    if (!options_data || !out) {
        return;
    }

    dhcp_options_clear(out);

    size_t i = 0;
    while (i < options_length) {
        uint8_t option = options_data[i++];

        if (option == 0) {
            continue;
        }
        if (option == DHCP_OPT_END) {
            break;
        }
        if (i >= options_length) {
            break;
        }

        uint8_t length = options_data[i++];
        if ((size_t)length > (options_length - i)) {
            break;
        }

        const uint8_t* value = &options_data[i];

        if (option == DHCP_OPT_MSG_TYPE && length == 1) {
            out->has_msg_type = true;
            out->msg_type = value[0];
        } else if (option == DHCP_OPT_SERVER_ID && length == 4) {
            out->has_server_id = true;
            memcpy(out->server_id, value, 4);
        } else if (option == DHCP_OPT_SUBNET_MASK && length == 4) {
            out->has_subnet_mask = true;
            memcpy(out->subnet_mask, value, 4);
        } else if (option == DHCP_OPT_ROUTER && length >= 4) {
            out->has_router = true;
            memcpy(out->router, value, 4);
        } else if (option == DHCP_OPT_DNS && length >= 4) {
            out->has_dns = true;
            memcpy(out->dns, value, 4);
        } else if (option == DHCP_OPT_LEASE_TIME && length == 4) {
            out->has_lease_time = true;
            out->lease_time = read_be32(value);
        }

        i += length;
    }
}

static size_t dhcp_prepare_base(uint8_t* packet, size_t capacity, uint32_t xid) {
    if (!packet || capacity < 240u) {
        return 0;
    }

    memset(packet, 0, capacity);

    packet[0] = 1;
    packet[1] = 1;
    packet[2] = 6;
    packet[3] = 0;
    write_be32(&packet[4], xid);
    write_be16(&packet[8], 0);
    write_be16(&packet[10], 0x8000u);
    memcpy(&packet[28], g_rtl8139.mac, 6);

    packet[236] = DHCP_MAGIC_COOKIE0;
    packet[237] = DHCP_MAGIC_COOKIE1;
    packet[238] = DHCP_MAGIC_COOKIE2;
    packet[239] = DHCP_MAGIC_COOKIE3;

    return 240u;
}

static bool dhcp_send_discover(void) {
    uint8_t packet[320];
    size_t offset = dhcp_prepare_base(packet, sizeof(packet), g_dhcp.xid);
    if (offset == 0) {
        return false;
    }

    packet[offset++] = DHCP_OPT_MSG_TYPE;
    packet[offset++] = 1;
    packet[offset++] = DHCP_MSG_DISCOVER;

    packet[offset++] = DHCP_OPT_PARAM_LIST;
    packet[offset++] = 3;
    packet[offset++] = DHCP_OPT_SUBNET_MASK;
    packet[offset++] = DHCP_OPT_ROUTER;
    packet[offset++] = DHCP_OPT_DNS;

    packet[offset++] = DHCP_OPT_CLIENT_ID;
    packet[offset++] = 7;
    packet[offset++] = 1;
    memcpy(&packet[offset], g_rtl8139.mac, 6);
    offset += 6;

    const char hostname[] = "SzczupakOS";
    packet[offset++] = DHCP_OPT_HOSTNAME;
    packet[offset++] = (uint8_t)(sizeof(hostname) - 1u);
    memcpy(&packet[offset], hostname, sizeof(hostname) - 1u);
    offset += sizeof(hostname) - 1u;

    packet[offset++] = DHCP_OPT_END;

    return net_send_udp_ipv4(IP_ZERO, IP_BROADCAST, DHCP_CLIENT_PORT, DHCP_SERVER_PORT,
                             packet, offset, true);
}

static bool dhcp_send_request(void) {
    if (g_dhcp.offered_ip == 0 || g_dhcp.server_ip == 0) {
        return false;
    }

    uint8_t packet[320];
    size_t offset = dhcp_prepare_base(packet, sizeof(packet), g_dhcp.xid);
    if (offset == 0) {
        return false;
    }

    packet[offset++] = DHCP_OPT_MSG_TYPE;
    packet[offset++] = 1;
    packet[offset++] = DHCP_MSG_REQUEST;

    packet[offset++] = DHCP_OPT_REQUESTED_IP;
    packet[offset++] = 4;
    write_be32(&packet[offset], g_dhcp.offered_ip);
    offset += 4;

    packet[offset++] = DHCP_OPT_SERVER_ID;
    packet[offset++] = 4;
    write_be32(&packet[offset], g_dhcp.server_ip);
    offset += 4;

    packet[offset++] = DHCP_OPT_PARAM_LIST;
    packet[offset++] = 3;
    packet[offset++] = DHCP_OPT_SUBNET_MASK;
    packet[offset++] = DHCP_OPT_ROUTER;
    packet[offset++] = DHCP_OPT_DNS;

    packet[offset++] = DHCP_OPT_CLIENT_ID;
    packet[offset++] = 7;
    packet[offset++] = 1;
    memcpy(&packet[offset], g_rtl8139.mac, 6);
    offset += 6;

    packet[offset++] = DHCP_OPT_END;

    return net_send_udp_ipv4(IP_ZERO, IP_BROADCAST, DHCP_CLIENT_PORT, DHCP_SERVER_PORT,
                             packet, offset, true);
}

static void dhcp_apply_ack(uint32_t yiaddr, const dhcp_options_t* options) {
    if (yiaddr != 0) {
        ip_from_u32(yiaddr, g_net_config.ip);
    } else {
        ip_from_u32(g_dhcp.offered_ip, g_net_config.ip);
    }

    if (options && options->has_subnet_mask) {
        memcpy(g_net_config.netmask, options->subnet_mask, 4);
    } else if (g_dhcp.has_offered_subnet) {
        memcpy(g_net_config.netmask, g_dhcp.offered_subnet, 4);
    } else {
        uint8_t default_mask[4] = {255, 255, 255, 0};
        memcpy(g_net_config.netmask, default_mask, 4);
    }

    if (options && options->has_router) {
        memcpy(g_net_config.gateway, options->router, 4);
    } else if (g_dhcp.has_offered_router) {
        memcpy(g_net_config.gateway, g_dhcp.offered_router, 4);
    } else {
        memset(g_net_config.gateway, 0, 4);
    }

    if (options && options->has_dns) {
        memcpy(g_net_config.dns, options->dns, 4);
    } else if (g_dhcp.has_offered_dns) {
        memcpy(g_net_config.dns, g_dhcp.offered_dns, 4);
    } else {
        memset(g_net_config.dns, 0, 4);
    }

    if (options && options->has_lease_time) {
        g_net_config.lease_time_seconds = options->lease_time;
    } else if (g_dhcp.has_lease_time) {
        g_net_config.lease_time_seconds = g_dhcp.lease_time;
    } else {
        g_net_config.lease_time_seconds = 0;
    }

    g_net_config.configured = true;
}

static void dhcp_handle_packet(const uint8_t* payload, size_t length, uint32_t source_ip) {
    if (!payload || length < 240u) {
        return;
    }

    if (payload[0] != 2 || payload[1] != 1 || payload[2] != 6) {
        return;
    }

    uint32_t xid = read_be32(&payload[4]);
    if (xid != g_dhcp.xid) {
        return;
    }

    if (memcmp(&payload[28], g_rtl8139.mac, 6) != 0) {
        return;
    }

    if (payload[236] != DHCP_MAGIC_COOKIE0 ||
        payload[237] != DHCP_MAGIC_COOKIE1 ||
        payload[238] != DHCP_MAGIC_COOKIE2 ||
        payload[239] != DHCP_MAGIC_COOKIE3) {
        return;
    }

    dhcp_options_t options;
    dhcp_parse_options(&payload[240], length - 240u, &options);
    if (!options.has_msg_type) {
        return;
    }

    uint32_t yiaddr = read_be32(&payload[16]);
    uint32_t siaddr = read_be32(&payload[20]);

    if (options.msg_type == DHCP_MSG_OFFER && g_dhcp.state == DHCP_STATE_WAIT_OFFER) {
        if (yiaddr == 0) {
            return;
        }

        g_dhcp.offered_ip = yiaddr;
        if (options.has_server_id) {
            g_dhcp.server_ip = read_be32(options.server_id);
        } else if (siaddr != 0) {
            g_dhcp.server_ip = siaddr;
        } else {
            g_dhcp.server_ip = source_ip;
        }

        g_dhcp.has_offered_subnet = false;
        if (options.has_subnet_mask) {
            memcpy(g_dhcp.offered_subnet, options.subnet_mask, 4);
            g_dhcp.has_offered_subnet = true;
        }

        g_dhcp.has_offered_router = false;
        if (options.has_router) {
            memcpy(g_dhcp.offered_router, options.router, 4);
            g_dhcp.has_offered_router = true;
        }

        g_dhcp.has_offered_dns = false;
        if (options.has_dns) {
            memcpy(g_dhcp.offered_dns, options.dns, 4);
            g_dhcp.has_offered_dns = true;
        }

        g_dhcp.has_lease_time = false;
        if (options.has_lease_time) {
            g_dhcp.lease_time = options.lease_time;
            g_dhcp.has_lease_time = true;
        }

        g_dhcp.state = DHCP_STATE_OFFER_READY;

        uint8_t offered_ip[4];
        ip_from_u32(g_dhcp.offered_ip, offered_ip);
        serial_write("[NET] DHCP offer received: ");
        serial_write_ip(offered_ip);
        serial_write("\n");
    } else if (options.msg_type == DHCP_MSG_ACK && g_dhcp.state == DHCP_STATE_WAIT_ACK) {
        dhcp_apply_ack(yiaddr, &options);
        g_dhcp.state = DHCP_STATE_BOUND;

        serial_write("[NET] DHCP lease acquired: ");
        serial_write_ip(g_net_config.ip);
        serial_write(" (gw ");
        serial_write_ip(g_net_config.gateway);
        serial_write(", mask ");
        serial_write_ip(g_net_config.netmask);
        serial_write(")\n");
    } else if (options.msg_type == DHCP_MSG_NAK && g_dhcp.state == DHCP_STATE_WAIT_ACK) {
        serial_write("[NET] DHCP NAK received, restarting discovery\n");
        g_dhcp.state = DHCP_STATE_WAIT_OFFER;
        dhcp_send_discover();
    }
}

