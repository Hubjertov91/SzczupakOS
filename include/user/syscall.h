#ifndef SYSCALL_H
#define SYSCALL_H

#include <stdint.h>

#define SYS_EXIT    0
#define SYS_WRITE   1
#define SYS_READ    2
#define SYS_GETPID  3
#define SYS_GETTIME 4
#define SYS_SLEEP   5
#define SYS_CLEAR   6
#define SYS_SYSINFO 7
#define SYS_FORK    8
#define SYS_EXEC    9
#define SYS_FB_PUTPIXEL 10
#define SYS_FB_CLEAR    11
#define SYS_FB_RECT     12
#define SYS_FB_INFO     13
#define SYS_FB_PUTCHAR  14
#define SYS_FB_PUTCHAR_PSF 15
#define SYS_LISTDIR     16
#define SYS_NET_INFO    17
#define SYS_NET_PING    18
#define SYS_NET_RESOLVE 19
#define SYS_NET_STATS   20
#define SYS_NET_TRACE_PROBE 21
#define SYS_NET_TCP_PROBE 22
#define SYS_FS_TOUCH    23
#define SYS_FS_MKDIR    24

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

long syscall0(long n);
long syscall1(long n, long a1);
long syscall2(long n, long a1, long a2);
long syscall3(long n, long a1, long a2, long a3);
long syscall4(long n, long a1, long a2, long a3, long a4);
long syscall5(long n, long a1, long a2, long a3, long a4, long a5);

void sys_exit(int code);
long sys_write(const char* buf, long len);
long sys_read(char* buf, long len);
long sys_getpid(void);
long sys_gettime(void);
void sys_sleep(long ms);
void sys_clear(void);
long sys_sysinfo(struct sysinfo* info);
long sys_fork(void);
long sys_exec(const char* cmdline);
long sys_fb_putpixel(uint32_t x, uint32_t y, uint32_t color);
long sys_fb_clear(uint32_t color);
long sys_fb_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
long sys_fb_info(struct fb_info* info);
long sys_fb_putchar(uint32_t x, uint32_t y, char c, uint32_t fg, uint32_t bg);
long sys_fb_putchar_psf(uint32_t x, uint32_t y, char c, uint32_t fg, uint32_t bg);
long sys_listdir(const char* path, char* buf, long len);
long sys_net_info(struct net_info* info);
long sys_net_ping(const uint8_t ip[4], uint32_t timeout_ms, uint32_t* out_rtt_ms);
long sys_net_resolve(const char* hostname, uint32_t timeout_ms, uint8_t out_ip[4]);
long sys_net_stats(struct net_stats* stats);
long sys_net_trace_probe(const struct net_trace_probe_req* req, struct net_trace_probe_rsp* rsp);
long sys_net_tcp_probe(const struct net_tcp_probe_req* req, struct net_tcp_probe_rsp* rsp);
long sys_fs_touch(const char* path);
long sys_fs_mkdir(const char* path);

#endif
