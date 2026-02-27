#include <syscall.h>

long syscall0(long n) {
    long ret;
    __asm__ volatile(
        "syscall"
        : "=a"(ret)
        : "a"(n)
        : "rcx", "r11", "memory"
    );
    return ret;
}

long syscall1(long n, long a1) {
    long ret;
    __asm__ volatile(
        "syscall"
        : "=a"(ret)
        : "a"(n), "D"(a1)
        : "rcx", "r11", "memory"
    );
    return ret;
}

long syscall2(long n, long a1, long a2) {
    long ret;
    __asm__ volatile(
        "syscall"
        : "=a"(ret)
        : "a"(n), "D"(a1), "S"(a2)
        : "rcx", "r11", "memory"
    );
    return ret;
}

long syscall3(long n, long a1, long a2, long a3) {
    long ret;
    __asm__ volatile(
        "syscall"
        : "=a"(ret)
        : "a"(n), "D"(a1), "S"(a2), "d"(a3)
        : "rcx", "r11", "memory"
    );
    return ret;
}

long syscall4(long n, long a1, long a2, long a3, long a4) {
    long ret;
    register long r10 __asm__("r10") = a4;
    __asm__ volatile(
        "syscall"
        : "=a"(ret)
        : "a"(n), "D"(a1), "S"(a2), "d"(a3), "r"(r10)
        : "rcx", "r11", "memory"
    );
    return ret;
}

long syscall5(long n, long a1, long a2, long a3, long a4, long a5) {
    long ret;
    register long r10 __asm__("r10") = a4;
    register long r8 __asm__("r8") = a5;
    __asm__ volatile(
        "syscall"
        : "=a"(ret)
        : "a"(n), "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8)
        : "rcx", "r11", "memory"
    );
    return ret;
}

void sys_exit(int code) {
    syscall1(SYS_EXIT, (long)code);
    for(;;);
}

long sys_write(const char* buf, long len) {
    return syscall2(SYS_WRITE, (long)buf, len);
}

long sys_read(char* buf, long len) {
    return syscall2(SYS_READ, (long)buf, len);
}

long sys_getpid(void) {
    return syscall0(SYS_GETPID);
}

long sys_gettime(void) {
    return syscall0(SYS_GETTIME);
}

void sys_sleep(long ms) {
    syscall1(SYS_SLEEP, ms);
}

void sys_clear(void) {
    syscall0(SYS_CLEAR);
}

long sys_sysinfo(struct sysinfo* info) {
    return syscall1(SYS_SYSINFO, (long)info);
}

long sys_fork(void) {
    return syscall0(SYS_FORK);
}

long sys_exec(const char* cmdline) {
    return syscall1(SYS_EXEC, (long)cmdline);
}

long sys_waitpid(int32_t pid, int32_t* out_exit_code) {
    return syscall2(SYS_WAITPID, (long)pid, (long)out_exit_code);
}

long sys_fb_putpixel(uint32_t x, uint32_t y, uint32_t color) {
    return syscall3(SYS_FB_PUTPIXEL, x, y, color);
}

long sys_fb_getpixel(uint32_t x, uint32_t y) {
    return syscall2(SYS_FB_GETPIXEL, x, y);
}

long sys_fb_clear(uint32_t color) {
    return syscall1(SYS_FB_CLEAR, color);
}

long sys_fb_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    return syscall5(SYS_FB_RECT, x, y, w, h, color);
}

long sys_fb_info(struct fb_info* info) {
    return syscall1(SYS_FB_INFO, (long)info);
}

long sys_fb_putchar(uint32_t x, uint32_t y, char c, uint32_t fg, uint32_t bg) {
    return syscall5(SYS_FB_PUTCHAR, x, y, (long)c, fg, bg);
}

long sys_fb_putchar_psf(uint32_t x, uint32_t y, char c, uint32_t fg, uint32_t bg) {
    return syscall5(SYS_FB_PUTCHAR_PSF, x, y, c, fg, bg);
}

long sys_listdir(const char* path, char* buf, long len) {
    return syscall3(SYS_LISTDIR, (long)path, (long)buf, len);
}

long sys_net_info(struct net_info* info) {
    return syscall1(SYS_NET_INFO, (long)info);
}

long sys_net_ping(const uint8_t ip[4], uint32_t timeout_ms, uint32_t* out_rtt_ms) {
    return syscall3(SYS_NET_PING, (long)ip, (long)timeout_ms, (long)out_rtt_ms);
}

long sys_net_resolve(const char* hostname, uint32_t timeout_ms, uint8_t out_ip[4]) {
    return syscall3(SYS_NET_RESOLVE, (long)hostname, (long)timeout_ms, (long)out_ip);
}

long sys_net_stats(struct net_stats* stats) {
    return syscall1(SYS_NET_STATS, (long)stats);
}

long sys_net_trace_probe(const struct net_trace_probe_req* req, struct net_trace_probe_rsp* rsp) {
    return syscall2(SYS_NET_TRACE_PROBE, (long)req, (long)rsp);
}

long sys_net_tcp_probe(const struct net_tcp_probe_req* req, struct net_tcp_probe_rsp* rsp) {
    return syscall2(SYS_NET_TCP_PROBE, (long)req, (long)rsp);
}

long sys_net_http_get(const struct net_http_get_req* req, struct net_http_get_rsp* rsp) {
    return syscall2(SYS_NET_HTTP_GET, (long)req, (long)rsp);
}

long sys_pci_get_count(void) {
    return syscall0(SYS_PCI_GET_COUNT);
}

long sys_pci_get_device(uint16_t index, struct pci_device_info* out_info) {
    return syscall2(SYS_PCI_GET_DEVICE, (long)index, (long)out_info);
}

long sys_usb_get_count(void) {
    return syscall0(SYS_USB_GET_COUNT);
}

long sys_usb_get_controller(uint16_t index, struct usb_controller_info* out_info) {
    return syscall2(SYS_USB_GET_CONTROLLER, (long)index, (long)out_info);
}

long sys_fs_touch(const char* path) {
    return syscall1(SYS_FS_TOUCH, (long)path);
}

long sys_fs_mkdir(const char* path) {
    return syscall1(SYS_FS_MKDIR, (long)path);
}

long sys_fs_delete(const char* path) {
    return syscall1(SYS_FS_DELETE, (long)path);
}

long sys_fs_read(const char* path, uint64_t offset, char* buf, uint32_t size) {
    return syscall4(SYS_FS_READ, (long)path, (long)offset, (long)buf, (long)size);
}

long sys_fs_write(const char* path, uint64_t offset, const char* buf, uint32_t size) {
    return syscall4(SYS_FS_WRITE, (long)path, (long)offset, (long)buf, (long)size);
}

long sys_kb_poll(void) {
    return syscall0(SYS_KB_POLL);
}

long sys_mouse_poll(struct mouse_event* event) {
    return syscall1(SYS_MOUSE_POLL, (long)event);
}

long sys_pty_open(void) {
    return syscall0(SYS_PTY_OPEN);
}

long sys_pty_close(int32_t pty_id) {
    return syscall1(SYS_PTY_CLOSE, (long)pty_id);
}

long sys_pty_read(int32_t pty_id, char* buf, uint32_t size) {
    return syscall3(SYS_PTY_READ, (long)pty_id, (long)buf, (long)size);
}

long sys_pty_write(int32_t pty_id, const char* buf, uint32_t size) {
    return syscall3(SYS_PTY_WRITE, (long)pty_id, (long)buf, (long)size);
}

long sys_pty_spawn(const char* cmdline, int32_t pty_id) {
    return syscall2(SYS_PTY_SPAWN, (long)cmdline, (long)pty_id);
}

long sys_pty_out_avail(int32_t pty_id) {
    return syscall1(SYS_PTY_OUT_AVAIL, (long)pty_id);
}

long sys_pty_in_avail(int32_t pty_id) {
    return syscall1(SYS_PTY_IN_AVAIL, (long)pty_id);
}
