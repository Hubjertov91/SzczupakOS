#include <net/net.h>

#include <drivers/pit.h>
#include <drivers/serial.h>
#include <drivers/pci.h>
#include <kernel/io.h>
#include <kernel/string.h>

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
#define ICMP_TYPE_TIME_EXCEEDED 11u

#define NET_DEFAULT_PING_TIMEOUT_MS 1200u
#define NET_DEFAULT_TCP_TIMEOUT_MS 1200u
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

#define DNS_SERVER_PORT    53u
#define DNS_PACKET_MAX     512u
#define DNS_DEFAULT_TIMEOUT_MS 2000u
#define DNS_DEFAULT_RETRY_MS   500u
#define DNS_MIN_SRC_PORT   49152u
#define DNS_MAX_HOSTNAME_LEN 253u
#define DNS_CACHE_SIZE     16u
#define DNS_MIN_TTL_S      30u
#define DNS_MAX_TTL_S      86400u

#define TCP_FLAG_FIN 0x01u
#define TCP_FLAG_SYN 0x02u
#define TCP_FLAG_RST 0x04u
#define TCP_FLAG_PSH 0x08u
#define TCP_FLAG_ACK 0x10u
#define TCP_MIN_SRC_PORT 49152u

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
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t sequence_number;
    uint32_t ack_number;
    uint8_t data_offset_reserved;
    uint8_t flags;
    uint16_t window_size;
    uint16_t checksum;
    uint16_t urgent_pointer;
} tcp_header_t;

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
    bool accept_time_exceeded;
    uint16_t identifier;
    uint16_t sequence;
    uint32_t expected_src_ip;
    uint32_t reply_src_ip;
    uint8_t reply_icmp_type;
    uint64_t sent_tick;
    uint64_t received_tick;
} ping_state_t;

typedef struct {
    bool waiting;
    bool received;
    uint16_t txid;
    uint16_t src_port;
    uint32_t expected_server_ip;
    uint8_t resolved_ip[4];
    uint32_t ttl_seconds;
    uint64_t sent_tick;
    uint64_t received_tick;
} dns_query_state_t;

typedef struct {
    bool waiting;
    bool received;
    bool open;
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t expected_src_ip;
    uint32_t sequence;
    uint64_t sent_tick;
    uint64_t received_tick;
} tcp_probe_state_t;

typedef struct {
    bool valid;
    char hostname[DNS_MAX_HOSTNAME_LEN + 1];
    uint8_t ip[4];
    uint64_t expires_at_ticks;
    uint64_t updated_at_ticks;
} dns_cache_entry_t;

static rtl8139_state_t g_rtl8139;
static net_config_t g_net_config;
static dhcp_client_t g_dhcp;
static arp_cache_entry_t g_arp_cache[ARP_CACHE_SIZE];
static ping_state_t g_ping_state;
static dns_query_state_t g_dns_state;
static tcp_probe_state_t g_tcp_probe_state;
static dns_cache_entry_t g_dns_cache[DNS_CACHE_SIZE];
static net_stats_t g_net_stats;
static uint32_t g_next_dhcp_xid = 0x535A0001u;
static uint16_t g_ip_identification = 0x1200u;
static uint16_t g_ping_sequence = 1;
static uint16_t g_dns_txid = 1;
static uint16_t g_dns_src_port = DNS_MIN_SRC_PORT;
static uint16_t g_tcp_src_port = TCP_MIN_SRC_PORT;
static uint32_t g_tcp_probe_seq_seed = 0x535A7001u;
static volatile uint32_t g_net_poll_active = 0;

static uint8_t g_rtl8139_rx_buffer[RTL_RX_BUFFER_SIZE + RTL_RX_TAIL_PAD] __attribute__((aligned(16)));
static uint8_t g_rtl8139_tx_buffers[RTL_TX_BUFFER_COUNT][RTL_TX_BUFFER_SIZE] __attribute__((aligned(16)));
static uint8_t g_rx_frame[ETH_HEADER_SIZE + ETH_MTU + 64u];
static uint8_t g_tx_eth_frame[ETH_HEADER_SIZE + ETH_MTU];
static uint8_t g_tx_ipv4_packet[ETH_MTU];
static uint8_t g_tx_udp_packet[sizeof(udp_header_t) + ETH_MTU];
static uint8_t g_udp_verify_packet[sizeof(udp_header_t) + ETH_MTU];
static uint8_t g_tx_tcp_packet[sizeof(tcp_header_t) + ETH_MTU];
static uint8_t g_icmp_reply[ETH_MTU - sizeof(ipv4_header_t)];
static uint8_t g_icmp_ipv4_packet[ETH_MTU];
static uint8_t g_dns_query_packet[DNS_PACKET_MAX];

static const uint8_t IP_BROADCAST[4] = {255, 255, 255, 255};
static const uint8_t IP_ZERO[4] = {0, 0, 0, 0};

#include "net_utils.c"
#include "net_driver.c"
#include "net_ip.c"
#include "net_dhcp.c"
#include "net_icmp.c"
#include "net_dns.c"
#include "net_tcp.c"
#include "net_rx.c"
#include "net_api.c"
