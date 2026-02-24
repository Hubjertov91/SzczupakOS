#include <net/net.h>

#include <drivers/pit.h>
#include <drivers/serial.h>
#include <kernel/io.h>
#include <kernel/string.h>

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC

#define RTL8139_VENDOR_ID  0x10EC
#define RTL8139_DEVICE_ID  0x8139

#define RTL_IDR0           0x00
#define RTL_TSD0           0x10
#define RTL_TSAD0          0x20
#define RTL_RBSTART        0x30
#define RTL_CR             0x37
#define RTL_CAPR           0x38
#define RTL_IMR            0x3C
#define RTL_ISR            0x3E
#define RTL_TCR            0x40
#define RTL_RCR            0x44
#define RTL_CONFIG1        0x52

#define RTL_CR_RX_EMPTY    0x01
#define RTL_CR_TX_ENABLE   0x04
#define RTL_CR_RX_ENABLE   0x08
#define RTL_CR_RESET       0x10

#define RTL_TSD_TOK        (1u << 15)
#define RTL_TSD_TUN        (1u << 14)
#define RTL_TSD_TABT       (1u << 30)
#define RTL_TSD_OWC        (1u << 29)

#define RTL_RX_BUFFER_SIZE 8192u
#define RTL_RX_TAIL_PAD    (16u + 1500u)
#define RTL_TX_BUFFER_SIZE 2048u
#define RTL_TX_BUFFER_COUNT 4u

#define ETH_HEADER_SIZE    14u
#define ETH_TYPE_IPV4      0x0800
#define ETH_TYPE_ARP       0x0806

#define ETH_MTU            1500u

#define IP_PROTO_ICMP      1u
#define IP_PROTO_TCP       6u
#define IP_PROTO_UDP       17u

#define ICMP_TYPE_ECHO_REPLY   0u
#define ICMP_TYPE_ECHO_REQUEST 8u

#define NET_DEFAULT_PING_TIMEOUT_MS 1200u
#define NET_PING_IDENTIFIER 0x535Au

#define ARP_HTYPE_ETHERNET 1u
#define ARP_PTYPE_IPV4     0x0800u
#define ARP_HLEN_ETHERNET  6u
#define ARP_PLEN_IPV4      4u
#define ARP_OP_REQUEST     1u
#define ARP_OP_REPLY       2u

#define ARP_CACHE_SIZE     16u
#define ARP_CACHE_TTL_S    120u

#define DHCP_CLIENT_PORT   68u
#define DHCP_SERVER_PORT   67u

#define DHCP_MAGIC_COOKIE0 99u
#define DHCP_MAGIC_COOKIE1 130u
#define DHCP_MAGIC_COOKIE2 83u
#define DHCP_MAGIC_COOKIE3 99u

#define DHCP_MSG_DISCOVER  1u
#define DHCP_MSG_OFFER     2u
#define DHCP_MSG_REQUEST   3u
#define DHCP_MSG_ACK       5u
#define DHCP_MSG_NAK       6u

#define DHCP_OPT_SUBNET_MASK   1u
#define DHCP_OPT_ROUTER        3u
#define DHCP_OPT_DNS           6u
#define DHCP_OPT_HOSTNAME      12u
#define DHCP_OPT_REQUESTED_IP  50u
#define DHCP_OPT_LEASE_TIME    51u
#define DHCP_OPT_MSG_TYPE      53u
#define DHCP_OPT_SERVER_ID     54u
#define DHCP_OPT_PARAM_LIST    55u
#define DHCP_OPT_CLIENT_ID     61u
#define DHCP_OPT_END           255u

typedef struct __attribute__((packed)) {
    uint8_t dst_mac[6];
    uint8_t src_mac[6];
    uint16_t ethertype;
} eth_header_t;

typedef struct __attribute__((packed)) {
    uint8_t version_ihl;
    uint8_t dscp_ecn;
    uint16_t total_length;
    uint16_t identification;
    uint16_t flags_fragment;
    uint8_t ttl;
    uint8_t protocol;
    uint16_t header_checksum;
    uint32_t src_ip;
    uint32_t dst_ip;
} ipv4_header_t;

typedef struct __attribute__((packed)) {
    uint16_t src_port;
    uint16_t dst_port;
    uint16_t length;
    uint16_t checksum;
} udp_header_t;

typedef struct __attribute__((packed)) {
    uint16_t hardware_type;
    uint16_t protocol_type;
    uint8_t hardware_size;
    uint8_t protocol_size;
    uint16_t opcode;
    uint8_t sender_mac[6];
    uint8_t sender_ip[4];
    uint8_t target_mac[6];
    uint8_t target_ip[4];
} arp_packet_t;

typedef struct __attribute__((packed)) {
    uint8_t type;
    uint8_t code;
    uint16_t checksum;
    uint16_t identifier;
    uint16_t sequence;
} icmp_echo_header_t;

typedef enum {
    DHCP_STATE_IDLE = 0,
    DHCP_STATE_WAIT_OFFER,
    DHCP_STATE_OFFER_READY,
    DHCP_STATE_WAIT_ACK,
    DHCP_STATE_BOUND
} dhcp_state_t;

typedef struct {
    bool initialized;
    uint16_t io_base;
    uint8_t mac[6];
    uint32_t rx_offset;
    uint8_t tx_index;
} rtl8139_state_t;

typedef struct {
    bool has_msg_type;
    uint8_t msg_type;
    bool has_server_id;
    uint8_t server_id[4];
    bool has_subnet_mask;
    uint8_t subnet_mask[4];
    bool has_router;
    uint8_t router[4];
    bool has_dns;
    uint8_t dns[4];
    bool has_lease_time;
    uint32_t lease_time;
} dhcp_options_t;

typedef struct {
    dhcp_state_t state;
    uint32_t xid;
    uint32_t offered_ip;
    uint32_t server_ip;
    uint8_t offered_subnet[4];
    bool has_offered_subnet;
    uint8_t offered_router[4];
    bool has_offered_router;
    uint8_t offered_dns[4];
    bool has_offered_dns;
    uint32_t lease_time;
    bool has_lease_time;
} dhcp_client_t;

typedef struct {
    bool valid;
    uint8_t ip[4];
    uint8_t mac[6];
    uint64_t updated_at_ticks;
} arp_cache_entry_t;

typedef struct {
    bool waiting;
    bool received;
    uint16_t identifier;
    uint16_t sequence;
    uint32_t expected_src_ip;
    uint64_t sent_tick;
    uint64_t received_tick;
} ping_state_t;

static rtl8139_state_t g_rtl8139;
static net_config_t g_net_config;
static dhcp_client_t g_dhcp;
static arp_cache_entry_t g_arp_cache[ARP_CACHE_SIZE];
static ping_state_t g_ping_state;
static uint32_t g_next_dhcp_xid = 0x535A0001u;
static uint16_t g_ip_identification = 0x1200u;
static uint16_t g_ping_sequence = 1;
static bool g_tcp_notice_printed = false;
static volatile uint32_t g_net_poll_active = 0;

static uint8_t g_rtl8139_rx_buffer[RTL_RX_BUFFER_SIZE + RTL_RX_TAIL_PAD] __attribute__((aligned(16)));
static uint8_t g_rtl8139_tx_buffers[RTL_TX_BUFFER_COUNT][RTL_TX_BUFFER_SIZE] __attribute__((aligned(16)));
static uint8_t g_rx_frame[ETH_HEADER_SIZE + ETH_MTU + 64u];

static const uint8_t IP_BROADCAST[4] = {255, 255, 255, 255};
static const uint8_t IP_ZERO[4] = {0, 0, 0, 0};

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
            return true;
        }
    }

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

static uint32_t pci_read32(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset) {
    uint32_t address = (1u << 31) |
                       ((uint32_t)bus << 16) |
                       ((uint32_t)slot << 11) |
                       ((uint32_t)function << 8) |
                       ((uint32_t)offset & 0xFCu);
    outl(PCI_CONFIG_ADDRESS, address);
    return inl(PCI_CONFIG_DATA);
}

static void pci_write32(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset, uint32_t value) {
    uint32_t address = (1u << 31) |
                       ((uint32_t)bus << 16) |
                       ((uint32_t)slot << 11) |
                       ((uint32_t)function << 8) |
                       ((uint32_t)offset & 0xFCu);
    outl(PCI_CONFIG_ADDRESS, address);
    outl(PCI_CONFIG_DATA, value);
}

static bool rtl8139_find(uint8_t* out_bus, uint8_t* out_slot, uint8_t* out_function) {
    if (!out_bus || !out_slot || !out_function) {
        return false;
    }

    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t slot = 0; slot < 32; slot++) {
            uint32_t id = pci_read32((uint8_t)bus, slot, 0, 0x00);
            if ((id & 0xFFFFu) == 0xFFFFu) {
                continue;
            }

            uint32_t hdr = pci_read32((uint8_t)bus, slot, 0, 0x0C);
            uint8_t header_type = (uint8_t)((hdr >> 16) & 0xFFu);
            uint8_t function_count = (header_type & 0x80u) ? 8u : 1u;

            for (uint8_t function = 0; function < function_count; function++) {
                uint32_t function_id = pci_read32((uint8_t)bus, slot, function, 0x00);
                uint16_t vendor = (uint16_t)(function_id & 0xFFFFu);
                uint16_t device = (uint16_t)((function_id >> 16) & 0xFFFFu);
                if (vendor == RTL8139_VENDOR_ID && device == RTL8139_DEVICE_ID) {
                    *out_bus = (uint8_t)bus;
                    *out_slot = slot;
                    *out_function = function;
                    return true;
                }
            }
        }
    }

    return false;
}

static bool rtl8139_init(void) {
    uint8_t bus = 0;
    uint8_t slot = 0;
    uint8_t function = 0;

    if (!rtl8139_find(&bus, &slot, &function)) {
        serial_write("[NET] RTL8139 PCI device not found\n");
        return false;
    }

    uint32_t command = pci_read32(bus, slot, function, 0x04);
    command |= 0x00000005u;
    pci_write32(bus, slot, function, 0x04, command);

    uint32_t bar0 = pci_read32(bus, slot, function, 0x10);
    if ((bar0 & 0x1u) == 0) {
        serial_write("[NET] RTL8139 BAR0 is not I/O mapped\n");
        return false;
    }

    uint16_t io_base = (uint16_t)(bar0 & 0xFFFCu);

    memset(&g_rtl8139, 0, sizeof(g_rtl8139));
    g_rtl8139.io_base = io_base;

    outb((uint16_t)(io_base + RTL_CONFIG1), 0x00);

    outb((uint16_t)(io_base + RTL_CR), RTL_CR_RESET);
    for (uint32_t i = 0; i < 200000u; i++) {
        if ((inb((uint16_t)(io_base + RTL_CR)) & RTL_CR_RESET) == 0) {
            break;
        }
        __asm__ volatile("pause");
    }
    if (inb((uint16_t)(io_base + RTL_CR)) & RTL_CR_RESET) {
        serial_write("[NET] RTL8139 reset timed out\n");
        return false;
    }

    for (uint8_t i = 0; i < 6; i++) {
        g_rtl8139.mac[i] = inb((uint16_t)(io_base + RTL_IDR0 + i));
    }

    uint64_t rx_addr64 = (uint64_t)(uint64_t)&g_rtl8139_rx_buffer[0];
    if (rx_addr64 > 0xFFFFFFFFu) {
        serial_write("[NET] RTL8139 RX buffer above 4GB\n");
        return false;
    }

    outl((uint16_t)(io_base + RTL_RBSTART), (uint32_t)rx_addr64);

    for (uint8_t i = 0; i < RTL_TX_BUFFER_COUNT; i++) {
        uint64_t tx_addr64 = (uint64_t)(uint64_t)&g_rtl8139_tx_buffers[i][0];
        if (tx_addr64 > 0xFFFFFFFFu) {
            serial_write("[NET] RTL8139 TX buffer above 4GB\n");
            return false;
        }
        outl((uint16_t)(io_base + RTL_TSAD0 + (i * 4u)), (uint32_t)tx_addr64);
    }

    outw((uint16_t)(io_base + RTL_IMR), 0x0000u);
    outw((uint16_t)(io_base + RTL_ISR), 0xFFFFu);

    outl((uint16_t)(io_base + RTL_RCR), 0x0000000Fu | (1u << 7));
    outl((uint16_t)(io_base + RTL_TCR), 0x03000700u);

    outb((uint16_t)(io_base + RTL_CR), RTL_CR_TX_ENABLE | RTL_CR_RX_ENABLE);

    g_rtl8139.rx_offset = 0;
    g_rtl8139.tx_index = 0;
    g_rtl8139.initialized = true;

    serial_write("[NET] RTL8139 initialized, MAC ");
    serial_write_mac(g_rtl8139.mac);
    serial_write("\n");

    return true;
}

static bool rtl8139_send_frame(const uint8_t* frame, size_t length) {
    if (!g_rtl8139.initialized || !frame || length == 0) {
        return false;
    }

    if (length > RTL_TX_BUFFER_SIZE) {
        return false;
    }

    uint8_t index = g_rtl8139.tx_index;
    uint8_t* tx_buffer = g_rtl8139_tx_buffers[index];

    size_t tx_length = length;
    if (tx_length < 60u) {
        tx_length = 60u;
    }

    memcpy(tx_buffer, frame, length);
    if (tx_length > length) {
        memset(tx_buffer + length, 0, tx_length - length);
    }

    uint16_t io_base = g_rtl8139.io_base;
    outl((uint16_t)(io_base + RTL_TSD0 + (index * 4u)), (uint32_t)tx_length);

    uint32_t status = 0;
    for (uint32_t i = 0; i < 100000u; i++) {
        status = inl((uint16_t)(io_base + RTL_TSD0 + (index * 4u)));
        if (status & (RTL_TSD_TOK | RTL_TSD_TUN | RTL_TSD_TABT | RTL_TSD_OWC)) {
            break;
        }
        __asm__ volatile("pause");
    }

    g_rtl8139.tx_index = (uint8_t)((index + 1u) % RTL_TX_BUFFER_COUNT);

    return (status & RTL_TSD_TOK) != 0;
}

static uint8_t rtl8139_ring_read_u8(uint32_t offset) {
    return g_rtl8139_rx_buffer[offset % RTL_RX_BUFFER_SIZE];
}

static uint16_t rtl8139_ring_read_u16(uint32_t offset) {
    uint16_t low = rtl8139_ring_read_u8(offset);
    uint16_t high = rtl8139_ring_read_u8(offset + 1u);
    return (uint16_t)(low | (high << 8));
}

static void rtl8139_ring_copy(uint32_t offset, uint8_t* out, size_t length) {
    if (!out || length == 0) {
        return;
    }

    for (size_t i = 0; i < length; i++) {
        out[i] = rtl8139_ring_read_u8(offset + (uint32_t)i);
    }
}

static bool rtl8139_poll_frame(uint8_t* out, size_t out_capacity, size_t* out_length) {
    if (!g_rtl8139.initialized || !out || !out_length || out_capacity == 0) {
        return false;
    }

    uint16_t io_base = g_rtl8139.io_base;
    if (inb((uint16_t)(io_base + RTL_CR)) & RTL_CR_RX_EMPTY) {
        return false;
    }

    uint32_t offset = g_rtl8139.rx_offset;
    uint16_t status = rtl8139_ring_read_u16(offset);
    uint16_t packet_length_with_crc = rtl8139_ring_read_u16(offset + 2u);

    if ((status & 0x0001u) == 0 || packet_length_with_crc < 4u || packet_length_with_crc > 1792u) {
        g_rtl8139.rx_offset = 0;
        outw((uint16_t)(io_base + RTL_CAPR), 0x0000u);
        outw((uint16_t)(io_base + RTL_ISR), 0xFFFFu);
        return false;
    }

    size_t packet_length = (size_t)(packet_length_with_crc - 4u);
    if (packet_length > out_capacity) {
        packet_length = out_capacity;
    }

    rtl8139_ring_copy(offset + 4u, out, packet_length);

    uint32_t advanced = offset + (uint32_t)packet_length_with_crc + 4u;
    advanced = (advanced + 3u) & ~3u;
    g_rtl8139.rx_offset = advanced % RTL_RX_BUFFER_SIZE;

    uint16_t capr = (uint16_t)((g_rtl8139.rx_offset - 16u) & 0xFFFFu);
    outw((uint16_t)(io_base + RTL_CAPR), capr);
    outw((uint16_t)(io_base + RTL_ISR), 0x0001u);

    *out_length = packet_length;
    return true;
}

static bool net_send_ethernet_frame(const uint8_t dst_mac[6], uint16_t ethertype,
                                    const uint8_t* payload, size_t payload_length) {
    if (!g_rtl8139.initialized || !dst_mac || !payload || payload_length > ETH_MTU) {
        return false;
    }

    uint8_t frame[ETH_HEADER_SIZE + ETH_MTU];
    eth_header_t* eth = (eth_header_t*)frame;
    memcpy(eth->dst_mac, dst_mac, 6);
    memcpy(eth->src_mac, g_rtl8139.mac, 6);
    eth->ethertype = net_htons(ethertype);

    if (payload_length > 0) {
        memcpy(frame + ETH_HEADER_SIZE, payload, payload_length);
    }

    return rtl8139_send_frame(frame, ETH_HEADER_SIZE + payload_length);
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
                                  uint8_t protocol, const uint8_t* payload,
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

    uint8_t packet[ETH_MTU];
    memset(packet, 0, sizeof(packet));

    ipv4_header_t* ip = (ipv4_header_t*)packet;
    ip->version_ihl = 0x45;
    ip->dscp_ecn = 0;
    uint16_t ip_total_length = (uint16_t)(sizeof(ipv4_header_t) + payload_length);
    ip->total_length = net_htons(ip_total_length);
    ip->identification = net_htons(g_ip_identification++);
    ip->flags_fragment = net_htons(0x4000u);
    ip->ttl = 64;
    ip->protocol = protocol;
    ip->header_checksum = 0;
    ip->src_ip = net_htonl(ip_to_u32(src_ip));
    ip->dst_ip = net_htonl(ip_to_u32(dst_ip));
    ip->header_checksum = net_htons(ipv4_checksum((const uint8_t*)ip, sizeof(ipv4_header_t)));

    memcpy(packet + sizeof(ipv4_header_t), payload, payload_length);

    return net_send_ethernet_frame(dst_mac, ETH_TYPE_IPV4, packet, ip_total_length);
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

    uint8_t udp_packet[sizeof(udp_header_t) + ETH_MTU];
    udp_header_t* udp = (udp_header_t*)udp_packet;
    udp->src_port = net_htons(src_port);
    udp->dst_port = net_htons(dst_port);
    uint16_t udp_length = (uint16_t)(sizeof(udp_header_t) + payload_length);
    udp->length = net_htons(udp_length);
    udp->checksum = 0;

    uint8_t* udp_payload = udp_packet + sizeof(udp_header_t);
    memcpy(udp_payload, payload, payload_length);

    udp->checksum = net_htons(udp_checksum(ip_to_u32(src_ip), ip_to_u32(dst_ip), udp_packet, udp_length));

    return net_send_ipv4_payload(src_ip, dst_ip, IP_PROTO_UDP, udp_packet, udp_length, force_broadcast_eth);
}

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

static uint16_t icmp_checksum(const uint8_t* packet, size_t length) {
    return checksum_finalize(checksum_add_bytes(0, packet, length));
}

static void net_handle_icmp(const uint8_t* payload, size_t payload_length, uint32_t src_ip, uint32_t dst_ip,
                            const uint8_t src_mac[6]) {
    if (!payload || payload_length < sizeof(icmp_echo_header_t)) {
        return;
    }

    if (icmp_checksum(payload, payload_length) != 0) {
        return;
    }

    const icmp_echo_header_t* icmp = (const icmp_echo_header_t*)payload;
    uint16_t identifier = net_ntohs(icmp->identifier);
    uint16_t sequence = net_ntohs(icmp->sequence);

    if (icmp->type == ICMP_TYPE_ECHO_REQUEST && icmp->code == 0) {
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

        uint8_t reply[ETH_MTU - sizeof(ipv4_header_t)];
        memcpy(reply, payload, payload_length);

        icmp_echo_header_t* reply_hdr = (icmp_echo_header_t*)reply;
        reply_hdr->type = ICMP_TYPE_ECHO_REPLY;
        reply_hdr->code = 0;
        reply_hdr->checksum = 0;
        reply_hdr->checksum = net_htons(icmp_checksum(reply, payload_length));

        uint8_t ipv4_packet[ETH_MTU];
        ipv4_header_t* ip = (ipv4_header_t*)ipv4_packet;
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
        memcpy(ipv4_packet + sizeof(ipv4_header_t), reply, payload_length);

        uint8_t src_ip_bytes[4];
        ip_from_u32(src_ip, src_ip_bytes);
        arp_cache_update(src_ip_bytes, src_mac);
        net_send_ethernet_frame(src_mac, ETH_TYPE_IPV4, ipv4_packet, ip_total_length);
        return;
    }

    if (icmp->type == ICMP_TYPE_ECHO_REPLY && icmp->code == 0) {
        if (g_ping_state.waiting &&
            !g_ping_state.received &&
            identifier == g_ping_state.identifier &&
            sequence == g_ping_state.sequence &&
            src_ip == g_ping_state.expected_src_ip) {
            g_ping_state.received = true;
            g_ping_state.received_tick = pit_get_ticks();
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
        uint8_t udp_copy[sizeof(udp_header_t) + ETH_MTU];
        if (udp_length <= sizeof(udp_copy)) {
            memcpy(udp_copy, payload, udp_length);
            ((udp_header_t*)udp_copy)->checksum = udp->checksum;
            uint16_t verify = udp_checksum(src_ip, dst_ip, udp_copy, udp_length);
            if (verify != 0xFFFFu && verify != 0u) {
                return;
            }
        }
    }

    if (src_port == DHCP_SERVER_PORT && dst_port == DHCP_CLIENT_PORT) {
        dhcp_handle_packet(udp_payload, udp_payload_length, src_ip);
    }
}

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
        if (!g_tcp_notice_printed) {
            serial_write("[NET] TCP segment received (TCP engine not implemented yet)\n");
            g_tcp_notice_printed = true;
        }
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
        net_handle_ipv4(payload, payload_length, eth->src_mac);
    } else if (ethertype == ETH_TYPE_ARP) {
        net_handle_arp(payload, payload_length);
    }
}

bool net_init(void) {
    memset(&g_net_config, 0, sizeof(g_net_config));
    memset(&g_dhcp, 0, sizeof(g_dhcp));
    memset(&g_ping_state, 0, sizeof(g_ping_state));
    arp_cache_clear();
    g_ping_sequence = 1;
    g_tcp_notice_printed = false;

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
    out_info->link_up = g_rtl8139.initialized;
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

bool net_ping_ipv4(const uint8_t dst_ip[4], uint32_t timeout_ms, uint32_t* out_rtt_ms) {
    if (!dst_ip || !g_rtl8139.initialized || !g_net_config.configured) {
        return false;
    }

    if (ip_is_zero(dst_ip) || ip_is_broadcast(dst_ip) || ip_is_zero(g_net_config.ip)) {
        return false;
    }

    if (ip_equal(dst_ip, g_net_config.ip)) {
        if (out_rtt_ms) {
            *out_rtt_ms = 0;
        }
        return true;
    }

    uint32_t frequency = pit_get_frequency();
    if (frequency == 0) {
        return false;
    }

    if (timeout_ms == 0) {
        timeout_ms = NET_DEFAULT_PING_TIMEOUT_MS;
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
    g_ping_state.sent_tick = send_tick;

    if (!net_send_ipv4_payload(g_net_config.ip, dst_ip, IP_PROTO_ICMP, icmp_packet, icmp_length, false)) {
        g_ping_state.waiting = false;
        return false;
    }

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
            g_ping_state.waiting = false;
            return true;
        }
        __asm__ volatile("pause");
    }

    g_ping_state.waiting = false;
    return false;
}
