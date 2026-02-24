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

bool net_init(void);
void net_poll(void);
bool net_configure_dhcp(uint32_t timeout_ms);
bool net_configure_static(const uint8_t ip[4], const uint8_t netmask[4],
                          const uint8_t gateway[4], const uint8_t dns[4]);
bool net_is_ready(void);
const net_config_t* net_get_config(void);
bool net_get_info(net_info_t* out_info);
bool net_ping_ipv4(const uint8_t dst_ip[4], uint32_t timeout_ms, uint32_t* out_rtt_ms);

#endif
