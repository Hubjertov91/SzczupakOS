#ifndef _KERNEL_NET_NET_H
#define _KERNEL_NET_NET_H

#include <kernel/stdint.h>

typedef struct {
    bool configured;
    uint8_t ip[4];
    uint8_t netmask[4];
    uint8_t gateway[4];
    uint8_t dns[4];
    uint32_t lease_time_seconds;
} net_config_t;

typedef struct {
    bool link_up;
    bool configured;
    uint8_t mac[6];
    uint8_t ip[4];
    uint8_t netmask[4];
    uint8_t gateway[4];
    uint8_t dns[4];
    uint32_t lease_time_seconds;
} net_info_t;

typedef struct {
    uint64_t rx_frames;
    uint64_t tx_frames;
    uint64_t rx_ipv4;
    uint64_t rx_arp;
    uint64_t rx_icmp;
    uint64_t rx_udp;
    uint64_t rx_tcp;
    uint64_t rx_dhcp;
    uint64_t rx_dns;
    uint64_t tx_ipv4;
    uint64_t tx_arp;
    uint64_t tx_icmp;
    uint64_t tx_udp;
    uint64_t arp_cache_hits;
    uint64_t arp_cache_misses;
    uint64_t dns_cache_hits;
    uint64_t dns_cache_misses;
    uint64_t dns_queries;
    uint64_t dns_timeouts;
    uint32_t arp_cache_entries;
    uint32_t dns_cache_entries;
} net_stats_t;

bool net_init(void);
void net_poll(void);
bool net_configure_dhcp(uint32_t timeout_ms);
bool net_configure_static(const uint8_t ip[4], const uint8_t netmask[4],
                          const uint8_t gateway[4], const uint8_t dns[4]);
bool net_is_ready(void);
const char* net_get_backend_name(void);
const net_config_t* net_get_config(void);
bool net_get_info(net_info_t* out_info);
bool net_get_stats(net_stats_t* out_stats);
bool net_ping_ipv4(const uint8_t dst_ip[4], uint32_t timeout_ms, uint32_t* out_rtt_ms);
bool net_trace_probe_ipv4(const uint8_t dst_ip[4], uint8_t ttl, uint32_t timeout_ms,
                          uint32_t* out_rtt_ms, uint8_t out_hop_ip[4], bool* out_reached_dst);
bool net_tcp_probe_ipv4(const uint8_t dst_ip[4], uint16_t dst_port, uint32_t timeout_ms,
                        bool* out_open, uint32_t* out_rtt_ms);
bool net_dns_resolve_ipv4(const char* hostname, uint8_t out_ip[4], uint32_t timeout_ms);
bool net_http_get_ipv4(const uint8_t dst_ip[4], uint16_t dst_port,
                       const char* host, const char* path,
                       uint32_t timeout_ms,
                       uint8_t* out_body, uint32_t out_body_capacity,
                       uint32_t* out_body_length,
                       uint16_t* out_status_code,
                       bool* out_truncated);

#endif
