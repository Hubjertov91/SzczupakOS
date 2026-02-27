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
#define SYSCALL_FS_TOUCH    23
#define SYSCALL_FS_MKDIR    24
#define SYSCALL_KB_POLL     25
#define SYSCALL_MOUSE_POLL  26
#define SYSCALL_FB_GETPIXEL 27
#define SYSCALL_PTY_OPEN    28
#define SYSCALL_PTY_CLOSE   29
#define SYSCALL_PTY_READ    30
#define SYSCALL_PTY_WRITE   31
#define SYSCALL_PTY_SPAWN   32
#define SYSCALL_PTY_OUT_AVAIL 33
#define SYSCALL_NET_HTTP_GET 34
#define SYSCALL_PCI_GET_COUNT 35
#define SYSCALL_PCI_GET_DEVICE 36
#define SYSCALL_USB_GET_COUNT 37
#define SYSCALL_USB_GET_CONTROLLER 38
#define SYSCALL_PTY_IN_AVAIL 39
#define SYSCALL_WAITPID 40
#define SYSCALL_FS_DELETE 41

#define NET_HTTP_HOST_MAX 128
#define NET_HTTP_PATH_MAX 192


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
    uint32_t font_width;
    uint32_t font_height;
};

struct mouse_event {
    int32_t x;
    int32_t y;
    int32_t dx;
    int32_t dy;
    uint8_t buttons;
    uint8_t changed;
    uint16_t _reserved;
    uint32_t seq;
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

struct net_http_get_req {
    uint8_t dst_ip[4];
    uint16_t dst_port;
    uint16_t _reserved0;
    uint32_t timeout_ms;
    uint64_t out_body_addr;
    uint32_t out_body_capacity;
    uint32_t _reserved1;
    char host[NET_HTTP_HOST_MAX];
    char path[NET_HTTP_PATH_MAX];
};

struct net_http_get_rsp {
    uint8_t ok;
    uint8_t truncated;
    uint16_t status_code;
    uint32_t body_length;
};

struct pci_device_info {
    uint8_t bus;
    uint8_t slot;
    uint8_t function;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;
    uint8_t revision_id;
    uint8_t header_type;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t is_pcie;
    uint8_t _reserved[3];
};

struct usb_controller_info {
    uint8_t bus;
    uint8_t slot;
    uint8_t function;
    uint8_t controller_type;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t is_pcie;
    uint8_t initialized;
    uint8_t port_count;
    uint8_t connected_ports;
    uint32_t io_base;
    uint64_t mmio_base;
};

bool syscall_init(void);

#endif
