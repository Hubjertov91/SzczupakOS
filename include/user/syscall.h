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
#define SYS_KB_POLL     25
#define SYS_MOUSE_POLL  26
#define SYS_FB_GETPIXEL 27
#define SYS_PTY_OPEN    28
#define SYS_PTY_CLOSE   29
#define SYS_PTY_READ    30
#define SYS_PTY_WRITE   31
#define SYS_PTY_SPAWN   32
#define SYS_PTY_OUT_AVAIL 33
#define SYS_NET_HTTP_GET 34
#define SYS_PCI_GET_COUNT 35
#define SYS_PCI_GET_DEVICE 36
#define SYS_USB_GET_COUNT 37
#define SYS_USB_GET_CONTROLLER 38
#define SYS_PTY_IN_AVAIL 39
#define SYS_WAITPID 40
#define SYS_FS_DELETE 41

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
long sys_waitpid(int32_t pid, int32_t* out_exit_code);
long sys_fb_putpixel(uint32_t x, uint32_t y, uint32_t color);
long sys_fb_getpixel(uint32_t x, uint32_t y);
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
long sys_net_http_get(const struct net_http_get_req* req, struct net_http_get_rsp* rsp);
long sys_pci_get_count(void);
long sys_pci_get_device(uint16_t index, struct pci_device_info* out_info);
long sys_usb_get_count(void);
long sys_usb_get_controller(uint16_t index, struct usb_controller_info* out_info);
long sys_fs_touch(const char* path);
long sys_fs_mkdir(const char* path);
long sys_fs_delete(const char* path);
long sys_kb_poll(void);
long sys_mouse_poll(struct mouse_event* event);
long sys_pty_open(void);
long sys_pty_close(int32_t pty_id);
long sys_pty_read(int32_t pty_id, char* buf, uint32_t size);
long sys_pty_write(int32_t pty_id, const char* buf, uint32_t size);
long sys_pty_spawn(const char* cmdline, int32_t pty_id);
long sys_pty_out_avail(int32_t pty_id);
long sys_pty_in_avail(int32_t pty_id);

#endif
