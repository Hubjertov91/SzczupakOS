#include <kernel/syscall.h>
#include <kernel/idt.h>
#include <kernel/serial.h>
#include <kernel/task.h>
#include <kernel/uaccess.h>
#include <kernel/terminal.h>
#include <kernel/scheduler.h>
#include <drivers/pit.h>
#include <kernel/pmm.h>

extern void syscall_handler_asm(void);

typedef struct {
    uint64_t rax;
    uint64_t rdx;
    uint64_t rsi;
    uint64_t rdi;
    uint64_t r8;
    uint64_t r9;
    uint64_t r10;
    uint64_t rbx;
    uint64_t rbp;
    uint64_t r12;
    uint64_t r13;
    uint64_t r14;
    uint64_t r15;
    uint64_t r11;
    uint64_t rcx;
} __attribute__((packed)) syscall_regs_t;

extern uint64_t pit_get_ticks(void);
extern uint64_t pmm_get_total_memory(void);
extern uint64_t pmm_get_used_memory(void);

static uint64_t sys_write(uint64_t str_addr, uint64_t size) {
    if (!str_addr || size == 0) return 0;
    if (size > 4096) return -1;
    char buffer[4096];
    if (copy_from_user(buffer, (const void*)str_addr, size) != 0) return -1;
    terminal_write(buffer, size);
    return size;
}

static uint64_t sys_read(uint64_t buf_addr, uint64_t size) {
    if (!buf_addr || size == 0) return 0;
    char c;
    size_t n = terminal_read(&c, 1);
    if (copy_to_user((void*)buf_addr, &c, 1) != 0) return -1;
    return n;
}

static uint64_t sys_getpid(void) { return task_get_pid(); }
static uint64_t sys_gettime(void) { return pit_get_ticks(); }

static uint64_t sys_sleep(uint64_t ms) {
    uint64_t start = pit_get_ticks();
    uint64_t target = start + ms / 10;
    while (pit_get_ticks() < target) {
        __asm__ volatile("sti; hlt; cli");
    }
    return 0;
}

static uint64_t sys_sysinfo(uint64_t info_addr) {
    if (!info_addr) return -1;
    struct sysinfo info;
    info.uptime = pit_get_ticks() / 100;
    info.total_memory = pmm_get_total_memory();
    info.free_memory = pmm_get_total_memory() - pmm_get_used_memory();
    info.nr_processes = 1;
    if (copy_to_user((void*)info_addr, &info, sizeof(info)) != 0) return -1;
    return 0;
}

static uint64_t sys_fork(void) {
    task_t* child = task_fork();
    if (!child) return -1;
    return (uint64_t)child->pid;
}

void syscall_handler(syscall_regs_t* regs) {
    switch (regs->rax) {
        case SYSCALL_EXIT:   task_exit(); break;
        case SYSCALL_WRITE:  regs->rax = sys_write(regs->rdi, regs->rsi); break;
        case SYSCALL_READ:   regs->rax = sys_read(regs->rdi, regs->rsi); break;
        case SYSCALL_GETPID: regs->rax = sys_getpid(); break;
        case SYSCALL_GETTIME:regs->rax = sys_gettime(); break;
        case SYSCALL_SLEEP:  regs->rax = sys_sleep(regs->rdi); break;
        case SYSCALL_CLEAR:  terminal_clear(); regs->rax = 0; break;
        case SYSCALL_SYSINFO:regs->rax = sys_sysinfo(regs->rdi); break;
        case SYSCALL_FORK:   regs->rax = sys_fork(); break;
        default:             regs->rax = -1; break;
    }
}

void syscall_init(void) {
    uint64_t star   = ((uint64_t)0x1B << 48) | ((uint64_t)0x08 << 32);
    uint64_t lstar  = (uint64_t)syscall_handler_asm;
    uint64_t sfmask = (1ULL << 9);

    __asm__ volatile("wrmsr" : : "c"(0xC0000081), "a"((uint32_t)star),   "d"((uint32_t)(star >> 32)));
    __asm__ volatile("wrmsr" : : "c"(0xC0000082), "a"((uint32_t)lstar),  "d"((uint32_t)(lstar >> 32)));
    __asm__ volatile("wrmsr" : : "c"(0xC0000084), "a"((uint32_t)sfmask), "d"((uint32_t)(sfmask >> 32)));

    uint64_t efer;
    __asm__ volatile("rdmsr" : "=a"(efer) : "c"(0xC0000080));
    efer |= 1;
    __asm__ volatile("wrmsr" : : "c"(0xC0000080), "a"((uint32_t)efer), "d"((uint32_t)(efer >> 32)));

    serial_write("[SYSCALL] Syscall interface initialized\n");
}