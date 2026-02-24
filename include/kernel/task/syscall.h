#ifndef _KERNEL_SYSCALL_H
#define _KERNEL_SYSCALL_H

#include "stdint.h"

#define SYSCALL_EXIT    0
#define SYSCALL_WRITE   1
#define SYSCALL_READ    2
#define SYSCALL_GETPID  3
#define SYSCALL_GETTIME 4
#define SYSCALL_SLEEP   5
#define SYSCALL_CLEAR   6
#define SYSCALL_SYSINFO 7
#define SYSCALL_FORK    8
#define SYSCALL_EXEC    9
#define SYSCALL_FB_PUTPIXEL 10
#define SYSCALL_FB_CLEAR    11
#define SYSCALL_FB_RECT     12
#define SYSCALL_FB_INFO     13
#define SYSCALL_FB_PUTCHAR  14
#define SYSCALL_FB_PUTCHAR_PSF 15   
#define SYSCALL_LISTDIR     16
#define SYSCALL_NET_INFO    17
#define SYSCALL_NET_PING    18
#define SYSCALL_NET_RESOLVE 19
#define SYSCALL_NET_STATS   20
#define SYSCALL_NET_TRACE_PROBE 21
#define SYSCALL_NET_TCP_PROBE 22


struct sysinfo {
    uint64_t uptime;
    uint64_t total_memory;
    uint64_t free_memory;
    uint32_t nr_processes;
};

struct fb_info {
    uint32_t width;
    uint32_t height;
    uint32_t bpp;
};

struct net_info {
    uint8_t link_up;
    uint8_t configured;
    uint8_t mac[6];
    uint8_t ip[4];
    uint8_t netmask[4];
    uint8_t gateway[4];
    uint8_t dns[4];
    uint32_t lease_time_seconds;
};

struct net_stats {
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
};

struct net_trace_probe_req {
    uint8_t dst_ip[4];
    uint8_t ttl;
    uint8_t _reserved[3];
    uint32_t timeout_ms;
};

struct net_trace_probe_rsp {
    uint8_t ok;
    uint8_t reached_dst;
    uint8_t _reserved[2];
    uint8_t hop_ip[4];
    uint32_t rtt_ms;
};

struct net_tcp_probe_req {
    uint8_t dst_ip[4];
    uint16_t dst_port;
    uint16_t _reserved0;
    uint32_t timeout_ms;
};

struct net_tcp_probe_rsp {
    uint8_t ok;
    uint8_t open;
    uint8_t _reserved[2];
    uint32_t rtt_ms;
};

bool syscall_init(void);

#endif
