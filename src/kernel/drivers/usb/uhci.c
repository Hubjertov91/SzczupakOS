#include "usb_priv.h"

static uint64_t uhci_ptr_to_phys(const uhci_host_t* host, const void* ptr) {
    return host->pages_phys + (uint64_t)((const uint8_t*)ptr - host->pages_virt);
}

static void uhci_td_set(volatile uhci_td_t* td,
                        uint32_t link,
                        uint8_t pid,
                        uint8_t addr,
                        uint8_t ep,
                        bool toggle,
                        uint16_t len,
                        uint32_t buf,
                        bool low_speed,
                        bool allow_short) {
    uint32_t token_len = (len == 0u) ? 0x7FFu : ((uint32_t)(len - 1u) & 0x7FFu);
    uint32_t status = UHCI_TD_CTRL_CERR | UHCI_TD_STATUS_ACTIVE;
    if (low_speed) status |= UHCI_TD_CTRL_LS;
    if (allow_short) status |= UHCI_TD_STATUS_SPD;

    td->link_ptr = link;
    td->ctrl_status = status;
    td->token = (uint32_t)pid |
                ((uint32_t)addr << 8) |
                ((uint32_t)ep << 15) |
                ((uint32_t)(toggle ? 1u : 0u) << 19) |
                (token_len << 21);
    td->buffer_ptr = buf;
}

static bool uhci_td_wait_inactive(volatile uhci_td_t* td, uint32_t poll_limit) {
    for (uint32_t i = 0; i < poll_limit; i++) {
        if ((td->ctrl_status & UHCI_TD_STATUS_ACTIVE) == 0u) {
            return true;
        }
        io_wait();
    }
    return false;
}

static bool uhci_td_has_error(uint32_t status, bool allow_nak) {
    if ((status & UHCI_TD_STATUS_ACTIVE) != 0u) return true;
    if ((status & (UHCI_TD_STATUS_STALLED |
                   UHCI_TD_STATUS_DBUFERR |
                   UHCI_TD_STATUS_BABBLE |
                   UHCI_TD_STATUS_CRC |
                   UHCI_TD_STATUS_BITSTUFF)) != 0u) {
        return true;
    }
    if (!allow_nak && (status & UHCI_TD_STATUS_NAK) != 0u) return true;
    return false;
}

static bool uhci_host_init(uhci_host_t* host, uint16_t io_base) {
    if (!host) return false;

    if (!host->ready) {
        uint64_t phys = pmm_alloc_pages(2u);
        if (!phys) {
            serial_write("[USB] UHCI: failed to allocate DMA pages\n");
            return false;
        }

        host->pages_phys = phys;
        host->pages_virt = (uint8_t*)PHYS_TO_VIRT(phys);
        memset(host->pages_virt, 0, 8192);

        host->frame_list = (volatile uint32_t*)host->pages_virt;
        host->qh = (volatile uhci_qh_t*)(host->pages_virt + 4096u);
        host->tds = (volatile uhci_td_t*)(host->pages_virt + 4096u + 32u);
        host->data_pool = host->pages_virt + 4096u + 32u + (sizeof(uhci_td_t) * UHCI_TD_POOL_COUNT);

        host->qh_phys = uhci_ptr_to_phys(host, (const void*)host->qh);
        host->tds_phys = uhci_ptr_to_phys(host, (const void*)host->tds);
        host->data_phys = uhci_ptr_to_phys(host, (const void*)host->data_pool);
    }

    host->io_base = io_base;

    for (uint32_t i = 0; i < 1024u; i++) {
        host->frame_list[i] = (uint32_t)(host->qh_phys & ~0xFu) | UHCI_LINK_QH;
    }

    host->qh->head_ptr = UHCI_LINK_TERMINATE;
    host->qh->element_ptr = UHCI_LINK_TERMINATE;

    outw((uint16_t)(io_base + UHCI_REG_USBCMD), 0u);
    (void)usb_wait_io16_set((uint16_t)(io_base + UHCI_REG_USBSTS), UHCI_USBSTS_HCHALTED, USB_PROBE_POLL_LIMIT);

    outw((uint16_t)(io_base + UHCI_REG_USBCMD), UHCI_USBCMD_HCRESET);
    if (!usb_wait_io16_clear((uint16_t)(io_base + UHCI_REG_USBCMD), UHCI_USBCMD_HCRESET, USB_PROBE_POLL_LIMIT)) {
        serial_write("[USB] UHCI reset timeout\n");
        return false;
    }
    usb_delay_ms(2u);

    outw((uint16_t)(io_base + UHCI_REG_USBSTS), 0xFFFFu);
    outw((uint16_t)(io_base + UHCI_REG_USBINTR), 0u);
    outw((uint16_t)(io_base + UHCI_REG_FRNUM), 0u);
    outl((uint16_t)(io_base + UHCI_REG_FLBASEADD), (uint32_t)(host->pages_phys & 0xFFFFF000u));
    outb((uint16_t)(io_base + UHCI_REG_SOFMOD), 0x40u);
    outw((uint16_t)(io_base + UHCI_REG_USBCMD), UHCI_USBCMD_CF | UHCI_USBCMD_RS);

    if (!usb_wait_io16_clear((uint16_t)(io_base + UHCI_REG_USBSTS), UHCI_USBSTS_HCHALTED, USB_PROBE_POLL_LIMIT)) {
        serial_write("[USB] UHCI failed to start scheduler\n");
        return false;
    }

    host->ready = true;
    return true;
}

static bool uhci_port_reset_enable(const uhci_host_t* host, uint8_t port, bool* out_low_speed) {
    if (!host || port == 0u || port > UHCI_MAX_PORTS || !out_low_speed) return false;

    uint16_t port_reg = (uint16_t)(host->io_base + UHCI_REG_PORTSC1 + (uint16_t)((port - 1u) * UHCI_PORT_STRIDE));
    uint16_t portsc = inw(port_reg);
    if ((portsc & UHCI_PORTSC_CCS) == 0u) {
        return false;
    }

    outw(port_reg, (uint16_t)(portsc | UHCI_PORTSC_CSC | UHCI_PORTSC_PEDC));
    usb_delay_ms(1u);

    outw(port_reg, (uint16_t)(portsc | UHCI_PORTSC_PR));
    usb_delay_ms(50u);

    outw(port_reg, (uint16_t)(portsc & (uint16_t)~UHCI_PORTSC_PR));
    usb_delay_ms(10u);

    portsc = inw(port_reg);
    outw(port_reg, (uint16_t)(portsc | UHCI_PORTSC_PED | UHCI_PORTSC_CSC | UHCI_PORTSC_PEDC));
    (void)usb_wait_io16_set(port_reg, UHCI_PORTSC_PED, USB_PROBE_POLL_LIMIT);
    usb_delay_ms(2u);

    portsc = inw(port_reg);
    if ((portsc & UHCI_PORTSC_CCS) == 0u || (portsc & UHCI_PORTSC_PED) == 0u) {
        return false;
    }

    *out_low_speed = (portsc & UHCI_PORTSC_LSDA) != 0u;
    return true;
}

static int uhci_control_transfer(uhci_host_t* host,
                                 bool low_speed,
                                 uint8_t addr,
                                 uint8_t max_packet,
                                 const usb_setup_packet_t* setup,
                                 uint8_t* data,
                                 uint16_t data_len,
                                 bool data_in) {
    if (!host || !host->ready || !setup || max_packet == 0u || max_packet > 64u) {
        return -1;
    }

    if ((uint32_t)data_len + 32u > UHCI_DATA_POOL_SIZE) {
        return -1;
    }

    memset((void*)host->tds, 0, sizeof(uhci_td_t) * UHCI_TD_POOL_COUNT);
    memset(host->data_pool, 0, UHCI_DATA_POOL_SIZE);

    memcpy(host->data_pool, setup, sizeof(*setup));
    if (!data_in && data_len > 0u && data) {
        memcpy(host->data_pool + 16u, data, data_len);
    }

    uint64_t setup_phys = host->data_phys;
    uint64_t data_phys = host->data_phys + 16u;

    uint32_t td_count = 0u;

    uhci_td_set(&host->tds[td_count++],
                UHCI_LINK_TERMINATE,
                UHCI_PID_SETUP,
                addr,
                0u,
                false,
                (uint16_t)sizeof(*setup),
                (uint32_t)setup_phys,
                low_speed,
                false);

    uint32_t data_td_start = td_count;
    uint32_t data_td_count = 0u;
    if (data_len > 0u) {
        uint16_t remaining = data_len;
        uint16_t offset = 0u;
        bool toggle = true;
        while (remaining > 0u) {
            if (td_count + 1u >= UHCI_TD_POOL_COUNT) {
                return -1;
            }
            uint16_t chunk = remaining;
            if (chunk > max_packet) chunk = max_packet;
            uhci_td_set(&host->tds[td_count++],
                        UHCI_LINK_TERMINATE,
                        data_in ? UHCI_PID_IN : UHCI_PID_OUT,
                        addr,
                        0u,
                        toggle,
                        chunk,
                        (uint32_t)(data_phys + offset),
                        low_speed,
                        data_in);
            remaining = (uint16_t)(remaining - chunk);
            offset = (uint16_t)(offset + chunk);
            toggle = !toggle;
            data_td_count++;
        }
    }

    if (td_count >= UHCI_TD_POOL_COUNT) {
        return -1;
    }
    uhci_td_set(&host->tds[td_count++],
                UHCI_LINK_TERMINATE,
                data_in ? UHCI_PID_OUT : UHCI_PID_IN,
                addr,
                0u,
                true,
                0u,
                0u,
                low_speed,
                true);

    for (uint32_t i = 0u; i + 1u < td_count; i++) {
        uint64_t next_phys = host->tds_phys + ((uint64_t)(i + 1u) * sizeof(uhci_td_t));
        host->tds[i].link_ptr = (uint32_t)(next_phys & ~0xFu);
    }
    host->tds[td_count - 1u].link_ptr = UHCI_LINK_TERMINATE;

    uint64_t first_td_phys = host->tds_phys;
    host->qh->element_ptr = (uint32_t)(first_td_phys & ~0xFu);

    volatile uhci_td_t* status_td = &host->tds[td_count - 1u];
    if (!uhci_td_wait_inactive(status_td, USB_PROBE_POLL_LIMIT * 8u)) {
        host->qh->element_ptr = UHCI_LINK_TERMINATE;
        return -1;
    }

    host->qh->element_ptr = UHCI_LINK_TERMINATE;

    if (uhci_td_has_error(host->tds[0].ctrl_status, false)) return -1;
    for (uint32_t i = 0u; i < data_td_count; i++) {
        if (uhci_td_has_error(host->tds[data_td_start + i].ctrl_status, false)) return -1;
    }
    if (uhci_td_has_error(status_td->ctrl_status, false)) return -1;

    if (data_in && data_len > 0u && data) {
        uint32_t actual_total = 0u;
        for (uint32_t i = 0u; i < data_td_count; i++) {
            uint32_t actual = host->tds[data_td_start + i].ctrl_status & 0x7FFu;
            if (actual != 0x7FFu) {
                actual += 1u;
            } else {
                actual = 0u;
            }
            if (actual > max_packet) actual = max_packet;
            if (actual_total + actual > data_len) {
                actual = (uint32_t)data_len - actual_total;
            }
            actual_total += actual;
            if (actual < max_packet) {
                break;
            }
        }
        if (actual_total > 0u) {
            memcpy(data, host->data_pool + 16u, actual_total);
        }
        return (int)actual_total;
    }

    return (int)data_len;
}

static int uhci_interrupt_in(uhci_host_t* host,
                             bool low_speed,
                             uint8_t addr,
                             uint8_t ep,
                             uint8_t max_packet,
                             bool* inout_toggle,
                             uint8_t* out,
                             uint16_t out_len) {
    if (!host || !host->ready || !inout_toggle || !out || max_packet == 0u) {
        return -1;
    }

    uint16_t req_len = out_len;
    if (req_len > max_packet) req_len = max_packet;
    if (req_len > 64u) req_len = 64u;

    memset((void*)host->tds, 0, sizeof(uhci_td_t));
    memset(host->data_pool, 0, 64u);

    volatile uhci_td_t* td = &host->tds[0];
    uint64_t td_phys = host->tds_phys;
    uint64_t data_phys = host->data_phys + 512u;

    uhci_td_set(td,
                UHCI_LINK_TERMINATE,
                UHCI_PID_IN,
                addr,
                ep,
                *inout_toggle,
                req_len,
                (uint32_t)data_phys,
                low_speed,
                true);

    host->qh->element_ptr = (uint32_t)(td_phys & ~0xFu);

    if (!uhci_td_wait_inactive(td, 4000u)) {
        host->qh->element_ptr = UHCI_LINK_TERMINATE;
        return 0;
    }

    uint32_t status = td->ctrl_status;
    host->qh->element_ptr = UHCI_LINK_TERMINATE;

    if ((status & UHCI_TD_STATUS_NAK) != 0u) {
        return 0;
    }
    if (uhci_td_has_error(status, false)) {
        return -1;
    }

    uint32_t actual = (status + 1u) & 0x7FFu;
    if (actual == 0u || actual > req_len) actual = req_len;

    memcpy(out, host->data_pool + 512u, actual);
    *inout_toggle = !(*inout_toggle);
    return (int)actual;
}

static bool uhci_control_request(uhci_host_t* host,
                                 bool low_speed,
                                 uint8_t addr,
                                 uint8_t max_packet,
                                 uint8_t bm_req_type,
                                 uint8_t b_req,
                                 uint16_t w_value,
                                 uint16_t w_index,
                                 uint16_t w_length,
                                 uint8_t* data) {
    usb_setup_packet_t setup;
    setup.bmRequestType = bm_req_type;
    setup.bRequest = b_req;
    setup.wValue = w_value;
    setup.wIndex = w_index;
    setup.wLength = w_length;

    bool dir_in = (bm_req_type & USB_DIR_IN) != 0u;
    return uhci_control_transfer(host, low_speed, addr, max_packet, &setup, data, w_length, dir_in) >= 0;
}

static uint8_t usb_hid_usage_to_ascii(uint8_t usage, bool shift, bool caps_lock) {
    if (usage >= 4u && usage <= 29u) {
        char base = (char)('a' + (usage - 4u));
        bool upper = (shift != caps_lock);
        return (uint8_t)(upper ? (base - ('a' - 'A')) : base);
    }

    switch (usage) {
        case 30u: return (uint8_t)(shift ? '!' : '1');
        case 31u: return (uint8_t)(shift ? '@' : '2');
        case 32u: return (uint8_t)(shift ? '#' : '3');
        case 33u: return (uint8_t)(shift ? '$' : '4');
        case 34u: return (uint8_t)(shift ? '%' : '5');
        case 35u: return (uint8_t)(shift ? '^' : '6');
        case 36u: return (uint8_t)(shift ? '&' : '7');
        case 37u: return (uint8_t)(shift ? '*' : '8');
        case 38u: return (uint8_t)(shift ? '(' : '9');
        case 39u: return (uint8_t)(shift ? ')' : '0');
        case 40u: return '\n';
        case 42u: return '\b';
        case 43u: return '\t';
        case 44u: return ' ';
        case 45u: return (uint8_t)(shift ? '_' : '-');
        case 46u: return (uint8_t)(shift ? '+' : '=');
        case 47u: return (uint8_t)(shift ? '{' : '[');
        case 48u: return (uint8_t)(shift ? '}' : ']');
        case 49u: return (uint8_t)(shift ? '|' : '\\');
        case 51u: return (uint8_t)(shift ? ':' : ';');
        case 52u: return (uint8_t)(shift ? '"' : '\'');
        case 53u: return (uint8_t)(shift ? '~' : '`');
        case 54u: return (uint8_t)(shift ? '<' : ',');
        case 55u: return (uint8_t)(shift ? '>' : '.');
        case 56u: return (uint8_t)(shift ? '?' : '/');
        default: return 0u;
    }
}

static uint64_t usb_hid_interval_to_ticks(uint8_t interval_ms) {
    uint64_t ticks = ((uint64_t)interval_ms + 9u) / 10u;
    if (ticks == 0u) ticks = 1u;
    return ticks;
}

static bool usb_hid_register_device(uint8_t type,
                                    uint8_t controller_index,
                                    uint8_t port,
                                    uint8_t addr,
                                    uint8_t ep_in,
                                    uint8_t max_packet,
                                    uint8_t interval,
                                    bool low_speed) {
    for (uint32_t i = 0; i < USB_HID_MAX_DEVICES; i++) {
        if (g_hid_devices[i].used) continue;

        memset(&g_hid_devices[i], 0, sizeof(g_hid_devices[i]));
        g_hid_devices[i].used = true;
        g_hid_devices[i].type = type;
        g_hid_devices[i].controller_index = controller_index;
        g_hid_devices[i].port_number = port;
        g_hid_devices[i].address = addr;
        g_hid_devices[i].endpoint_in = ep_in;
        g_hid_devices[i].max_packet = max_packet;
        g_hid_devices[i].interval = (interval == 0u) ? 1u : interval;
        g_hid_devices[i].low_speed = low_speed;
        g_hid_devices[i].toggle = false;
        g_hid_devices[i].caps_lock = false;
        g_hid_devices[i].interval_ticks = usb_hid_interval_to_ticks(g_hid_devices[i].interval);
        g_hid_devices[i].next_poll_tick = pit_get_ticks();
        memset(g_hid_devices[i].last_report, 0, sizeof(g_hid_devices[i].last_report));

        if (type == USB_HID_TYPE_KEYBOARD) {
            keyboard_set_usb_hid_active(true);
        } else if (type == USB_HID_TYPE_MOUSE) {
            mouse_set_usb_hid_active(true);
        }
        return true;
    }

    return false;
}

static void usb_hid_process_keyboard(usb_hid_device_t* hid, const uint8_t* report, uint32_t report_len) {
    if (!hid || !report || report_len < 8u) return;

    bool shift = ((report[0] & 0x22u) != 0u);
    uint64_t now_ticks = pit_get_ticks();
    uint8_t repeat_candidate = 0u;

    for (uint32_t i = 2u; i < 8u; i++) {
        uint8_t key = report[i];
        if (key == 0u) continue;

        bool was_pressed = false;
        for (uint32_t j = 2u; j < 8u; j++) {
            if (hid->last_report[j] == key) {
                was_pressed = true;
                break;
            }
        }
        if (was_pressed) continue;

        if (key == 57u) {
            hid->caps_lock = !hid->caps_lock;
            continue;
        }

        uint8_t c = usb_hid_usage_to_ascii(key, shift, hid->caps_lock);
        if (c != 0u) {
            keyboard_inject_char((char)c);
            if (repeat_candidate == 0u) {
                repeat_candidate = key;
            }
        }
    }

    if (repeat_candidate == 0u) {
        for (uint32_t i = 2u; i < 8u; i++) {
            uint8_t key = report[i];
            if (key == 0u || key == 57u) continue;
            uint8_t c = usb_hid_usage_to_ascii(key, shift, hid->caps_lock);
            if (c != 0u) {
                repeat_candidate = key;
                break;
            }
        }
    }

    if (repeat_candidate == 0u) {
        hid->repeat_usage = 0u;
        hid->repeat_next_tick = 0u;
    } else if (repeat_candidate != hid->repeat_usage) {
        hid->repeat_usage = repeat_candidate;
        hid->repeat_next_tick = now_ticks + USB_KBD_REPEAT_DELAY_TICKS;
    } else if (hid->repeat_next_tick != 0u && now_ticks >= hid->repeat_next_tick) {
        uint8_t c = usb_hid_usage_to_ascii(repeat_candidate, shift, hid->caps_lock);
        if (c != 0u) {
            keyboard_inject_char((char)c);
        }
        do {
            hid->repeat_next_tick += USB_KBD_REPEAT_RATE_TICKS;
        } while (hid->repeat_next_tick <= now_ticks);
    }

    memcpy(hid->last_report, report, 8u);
}

static void usb_hid_process_mouse(usb_hid_device_t* hid, const uint8_t* report, uint32_t report_len) {
    if (!hid || !report || report_len < 3u) return;

    uint8_t buttons = report[0] & 0x07u;
    int8_t dx = (int8_t)report[1];
    int8_t dy = (int8_t)report[2];

    if (dx != 0 || dy != 0 || hid->last_report[0] != buttons) {
        mouse_inject_usb(dx, (int8_t)-dy, buttons);
    }

    hid->last_report[0] = buttons;
}

static bool uhci_enumerate_hid_on_port(uhci_host_t* host,
                                       uint8_t controller_index,
                                       uint8_t port,
                                       bool low_speed) {
    uint8_t dev_desc_head[8];
    memset(dev_desc_head, 0, sizeof(dev_desc_head));

    if (!uhci_control_request(host, low_speed, 0u, 8u,
                              USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_DEVICE,
                              USB_REQ_GET_DESCRIPTOR,
                              (uint16_t)((USB_DESC_DEVICE << 8) | 0u),
                              0u,
                              (uint16_t)sizeof(dev_desc_head),
                              dev_desc_head)) {
        serial_write("[USB] UHCI enum failed (dev-desc-8), port=");
        serial_write_dec((uint32_t)port);
        serial_write("\n");
        return false;
    }

    uint8_t max_packet0 = dev_desc_head[7];
    if (max_packet0 == 0u || max_packet0 > 64u) {
        max_packet0 = 8u;
    }

    if (g_next_usb_address == 0u || g_next_usb_address >= 127u) {
        return false;
    }
    uint8_t addr = g_next_usb_address++;

    if (!uhci_control_request(host, low_speed, 0u, max_packet0,
                              USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_DEVICE,
                              USB_REQ_SET_ADDRESS,
                              addr,
                              0u,
                              0u,
                              NULL)) {
        serial_write("[USB] UHCI enum failed (set-address), port=");
        serial_write_dec((uint32_t)port);
        serial_write("\n");
        return false;
    }

    for (uint32_t i = 0; i < 10000u; i++) {
        io_wait();
    }
    usb_delay_ms(2u);

    uint8_t dev_desc_buf[18];
    memset(dev_desc_buf, 0, sizeof(dev_desc_buf));
    if (!uhci_control_request(host, low_speed, addr, max_packet0,
                              USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_DEVICE,
                              USB_REQ_GET_DESCRIPTOR,
                              (uint16_t)((USB_DESC_DEVICE << 8) | 0u),
                              0u,
                              (uint16_t)sizeof(dev_desc_buf),
                              dev_desc_buf)) {
        serial_write("[USB] UHCI enum failed (dev-desc-18), port=");
        serial_write_dec((uint32_t)port);
        serial_write("\n");
        return false;
    }

    uint8_t cfg_head[9];
    memset(cfg_head, 0, sizeof(cfg_head));
    if (!uhci_control_request(host, low_speed, addr, max_packet0,
                              USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_DEVICE,
                              USB_REQ_GET_DESCRIPTOR,
                              (uint16_t)((USB_DESC_CONFIG << 8) | 0u),
                              0u,
                              (uint16_t)sizeof(cfg_head),
                              cfg_head)) {
        serial_write("[USB] UHCI enum failed (cfg-head), port=");
        serial_write_dec((uint32_t)port);
        serial_write("\n");
        return false;
    }

    uint16_t total_len = (uint16_t)cfg_head[2] | ((uint16_t)cfg_head[3] << 8);
    if (total_len < sizeof(usb_config_descriptor_t)) {
        serial_write("[USB] UHCI enum failed (cfg-total-len), port=");
        serial_write_dec((uint32_t)port);
        serial_write("\n");
        return false;
    }
    if (total_len > 255u) total_len = 255u;

    uint8_t cfg_buf[255];
    memset(cfg_buf, 0, sizeof(cfg_buf));
    if (!uhci_control_request(host, low_speed, addr, max_packet0,
                              USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_DEVICE,
                              USB_REQ_GET_DESCRIPTOR,
                              (uint16_t)((USB_DESC_CONFIG << 8) | 0u),
                              0u,
                              total_len,
                              cfg_buf)) {
        serial_write("[USB] UHCI enum failed (cfg-full), port=");
        serial_write_dec((uint32_t)port);
        serial_write("\n");
        return false;
    }

    usb_config_descriptor_t* cfg = (usb_config_descriptor_t*)cfg_buf;
    uint8_t config_value = cfg->bConfigurationValue;

    if (!uhci_control_request(host, low_speed, addr, max_packet0,
                              USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_DEVICE,
                              USB_REQ_SET_CONFIGURATION,
                              config_value,
                              0u,
                              0u,
                              NULL)) {
        serial_write("[USB] UHCI enum failed (set-config), port=");
        serial_write_dec((uint32_t)port);
        serial_write("\n");
        return false;
    }

    bool registered_any = false;
    uint8_t current_iface = 0xFFu;
    uint8_t current_proto = 0u;
    bool current_hid_iface = false;

    uint16_t pos = 0u;
    while (pos + 2u <= total_len) {
        uint8_t len = cfg_buf[pos];
        uint8_t type = cfg_buf[pos + 1u];
        if (len < 2u || pos + len > total_len) break;

        if (type == USB_DESC_INTERFACE && len >= sizeof(usb_interface_descriptor_t)) {
            usb_interface_descriptor_t* iface = (usb_interface_descriptor_t*)&cfg_buf[pos];
            current_iface = iface->bInterfaceNumber;
            current_proto = iface->bInterfaceProtocol;
            current_hid_iface = (iface->bInterfaceClass == USB_HID_CLASS);
        } else if (type == USB_DESC_ENDPOINT && current_hid_iface && len >= sizeof(usb_endpoint_descriptor_t)) {
            usb_endpoint_descriptor_t* ep = (usb_endpoint_descriptor_t*)&cfg_buf[pos];
            bool ep_in = (ep->bEndpointAddress & 0x80u) != 0u;
            bool ep_interrupt = (ep->bmAttributes & 0x03u) == 0x03u;
            if (ep_in && ep_interrupt) {
                uint8_t ep_mps = (uint8_t)(ep->wMaxPacketSize & 0x7Fu);
                if (ep_mps == 0u) ep_mps = 8u;
                uint8_t hid_type = USB_HID_TYPE_NONE;

                if (current_proto == USB_HID_PROTO_KEYBOARD) {
                    hid_type = USB_HID_TYPE_KEYBOARD;
                } else if (current_proto == USB_HID_PROTO_MOUSE) {
                    hid_type = USB_HID_TYPE_MOUSE;
                } else {
                    hid_type = (ep_mps <= 4u) ? USB_HID_TYPE_MOUSE : USB_HID_TYPE_KEYBOARD;
                }

                if (hid_type == USB_HID_TYPE_KEYBOARD) {
                    (void)uhci_control_request(host, low_speed, addr, max_packet0,
                                               USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
                                               USB_REQ_SET_IDLE,
                                               0u,
                                               current_iface,
                                               0u,
                                               NULL);
                }

                if (usb_hid_register_device(hid_type,
                                            controller_index,
                                            port,
                                            addr,
                                            (uint8_t)(ep->bEndpointAddress & 0x0Fu),
                                            ep_mps,
                                            ep->bInterval,
                                            low_speed)) {
                    serial_write("[USB] HID ");
                    serial_write(hid_type == USB_HID_TYPE_KEYBOARD ? "keyboard" : "mouse");
                    serial_write(" at addr=");
                    serial_write_dec(addr);
                    serial_write(", port=");
                    serial_write_dec((uint32_t)port);
                    serial_write("\n");
                    registered_any = true;
                }

                current_hid_iface = false;
            }
        }

        pos = (uint16_t)(pos + len);
    }

    if (!registered_any) {
        serial_write("[USB] UHCI enum: no usable HID endpoint, port=");
        serial_write_dec((uint32_t)port);
        serial_write("\n");
    }

    return registered_any;
}

bool usb_probe_uhci(const pci_device_t* dev, usb_controller_info_t* info, uint8_t controller_index) {
    if (!dev || !info || info->io_base == 0u || controller_index >= USB_MAX_CONTROLLERS) {
        return false;
    }

    (void)pci_set_command_bits(dev->bus, dev->slot, dev->function,
                               PCI_COMMAND_IO_SPACE | PCI_COMMAND_BUS_MASTER);

    uhci_host_t* host = &g_uhci_hosts[controller_index];
    if (!uhci_host_init(host, (uint16_t)(info->io_base & 0xFFFCu))) {
        return false;
    }

    uint8_t connected = 0u;
    for (uint8_t port = 1u; port <= UHCI_MAX_PORTS; port++) {
        bool low_speed = false;
        if (!uhci_port_reset_enable(host, port, &low_speed)) {
            continue;
        }

        connected++;
        serial_write("[USB] UHCI port ");
        serial_write_dec((uint32_t)port);
        serial_write(": connected, speed=");
        serial_write(usb_full_or_low_speed_name(low_speed));
        serial_write("\n");

        (void)uhci_enumerate_hid_on_port(host, controller_index, port, low_speed);
    }

    info->port_count = UHCI_MAX_PORTS;
    info->connected_ports = connected;
    return true;
}

void usb_poll(void) {
    if (!g_usb_scanned) {
        return;
    }

    uint64_t now_ticks = pit_get_ticks();

    for (uint32_t i = 0; i < USB_HID_MAX_DEVICES; i++) {
        usb_hid_device_t* hid = &g_hid_devices[i];
        if (!hid->used) continue;
        if (hid->controller_index >= USB_MAX_CONTROLLERS) continue;

        uhci_host_t* host = &g_uhci_hosts[hid->controller_index];
        if (!host->ready) continue;

        uint64_t interval_ticks = hid->interval_ticks;
        if (interval_ticks == 0u) {
            interval_ticks = usb_hid_interval_to_ticks(hid->interval);
            hid->interval_ticks = interval_ticks;
        }
        if (hid->next_poll_tick != 0u && now_ticks < hid->next_poll_tick) {
            continue;
        }
        hid->next_poll_tick = now_ticks + interval_ticks;

        uint8_t report[8];
        memset(report, 0, sizeof(report));

        int n = uhci_interrupt_in(host,
                                  hid->low_speed,
                                  hid->address,
                                  hid->endpoint_in,
                                  hid->max_packet,
                                  &hid->toggle,
                                  report,
                                  sizeof(report));
        if (n <= 0) continue;

        if (hid->type == USB_HID_TYPE_KEYBOARD) {
            usb_hid_process_keyboard(hid, report, (uint32_t)n);
        } else if (hid->type == USB_HID_TYPE_MOUSE) {
            usb_hid_process_mouse(hid, report, (uint32_t)n);
        }
    }
}
