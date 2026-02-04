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
    uint64_t rax, rbx, rcx, rdx;
    uint64_t rsi, rdi, rbp;
    uint64_t r8, r9, r10, r11;
    uint64_t r12, r13, r14, r15;
} syscall_regs_t;

extern uint64_t pit_get_ticks(void);

static uint64_t sys_write(uint64_t str_addr, uint64_t size) {
    if (!str_addr || size == 0) return 0;
    if (size > 4096) {
        serial_write("[SYSCALL] ERROR: Write buffer too large\n");
        return -1;
    }

    char buffer[4097];
    if (!copy_from_user(buffer, (const void*)str_addr, size)) {
        serial_write("[SYSCALL] ERROR: Invalid user pointer\n");
        return -1;
    }

    terminal_write(buffer, size);
    return size;
}

static uint64_t sys_read(uint64_t buf_addr, uint64_t size) {
    if (!buf_addr || size == 0) return 0;
    if (size > 256) {
        serial_write("[SYSCALL] ERROR: Read buffer too large\n");
        return -1;
    }

    scheduler_disable();

    char tmp_buf[256];
    size_t len = terminal_read(tmp_buf, size);

    if (!copy_to_user((void*)buf_addr, tmp_buf, len)) {
        scheduler_enable();
        serial_write("[SYSCALL] ERROR: Invalid user buffer\n");
        return -1;
    }

    scheduler_enable();
    return len;
}

static uint64_t sys_getpid(void) {
    return task_get_pid();
}

static uint64_t sys_gettime(void) {
    return pit_get_ticks();
}

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
    
    if (!copy_to_user((void*)info_addr, &info, sizeof(info))) {
        return -1;
    }
    
    return 0;
}

static uint64_t sys_fork(void) {
    task_t* child = task_fork();
    if (!child) {
        serial_write("[SYSCALL] fork() failed\n");
        return -1;
    }
    return (uint64_t)child->pid;
}

extern uint64_t pmm_get_total_memory(void);
extern uint64_t pmm_get_used_memory(void);

void syscall_handler(syscall_regs_t* regs) {
    switch (regs->rax) {
        case SYSCALL_EXIT:
            task_exit();
            break;
        case SYSCALL_WRITE:
            regs->rax = sys_write(regs->rdi, regs->rsi);
            break;
        case SYSCALL_READ:
            regs->rax = sys_read(regs->rdi, regs->rsi);
            break;
        case SYSCALL_GETPID:
            regs->rax = sys_getpid();
            break;
        case SYSCALL_GETTIME:
            regs->rax = sys_gettime();
            break;
        case SYSCALL_SLEEP:
            regs->rax = sys_sleep(regs->rdi);
            break;
        case SYSCALL_CLEAR:
            terminal_clear();
            regs->rax = 0;
            break;
        case SYSCALL_SYSINFO:
            regs->rax = sys_sysinfo(regs->rdi);
            break;
        case SYSCALL_FORK:
            regs->rax = sys_fork();
            break;
        default:
            regs->rax = -1;
            break;
    }
}

void syscall_init(void) {
    idt_set_gate(0x80, (uint64_t)syscall_handler_asm, 0xEE);
}