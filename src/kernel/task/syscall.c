#include <task/syscall.h>
#include <arch/idt.h>
#include <drivers/serial.h>
#include <task/task.h>
#include <mm/uaccess.h>
#include <mm/heap.h>
#include <kernel/terminal.h>
#include <fs/vfs.h>
#include <drivers/framebuffer.h>
#include <drivers/psf.h>

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

#define EXEC_PATH_MAX 256
#define EXEC_FILE_MAX (2 * 1024 * 1024)
#define LISTDIR_BUF_MAX 4096

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
    char buffer[4096];
    size_t pos = 0;
    
    while (pos < size - 1 && pos < 4096 - 1) {
        char c;
        size_t n = terminal_read(&c, 1);
        if (n <= 0) break;

        if (c == '\b') {
            if (pos > 0) {
                pos--;
                terminal_write("\b \b", 3);
            }
            continue;
        }

        if (c == '\r') c = '\n';

        if (c == '\n') {
            buffer[pos++] = c;
            terminal_write(&c, 1);
            break;
        }

        if ((unsigned char)c < 32 || (unsigned char)c > 126) {
            continue;
        }

        buffer[pos++] = c;
        terminal_write(&c, 1);
    }
    
    buffer[pos] = '\0';
    
    if (copy_to_user((void*)buf_addr, buffer, pos + 1) != 0) return -1;
    return pos;
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

static uint64_t sys_exec(uint64_t path_addr) {
    if (!path_addr) return (uint64_t)-1;

    char path[EXEC_PATH_MAX];
    size_t i = 0;
    for (; i < EXEC_PATH_MAX; i++) {
        if (copy_from_user(&path[i], (const void*)(path_addr + i), 1) != 0) {
            return (uint64_t)-1;
        }
        if (path[i] == '\0') break;
    }

    if (i == 0 || i == EXEC_PATH_MAX || path[0] != '/') {
        return (uint64_t)-1;
    }

    vfs_node_t* file = vfs_open(path, 0);
    if (!file || file->type != VFS_FILE || file->size == 0 || file->size > EXEC_FILE_MAX) {
        if (file) vfs_close(file);
        return (uint64_t)-1;
    }

    uint8_t* elf_data = (uint8_t*)kmalloc(file->size);
    if (!elf_data) {
        vfs_close(file);
        return (uint64_t)-1;
    }

    if (!vfs_read(file, elf_data, 0, file->size)) {
        kfree(elf_data);
        vfs_close(file);
        return (uint64_t)-1;
    }

    task_t* task = task_create_user(path, elf_data, file->size);
    kfree(elf_data);
    vfs_close(file);

    if (!task) {
        return (uint64_t)-1;
    }
    return task->pid;
}

static uint64_t sys_listdir(uint64_t path_addr, uint64_t buf_addr, uint64_t buf_size) {
    if (!path_addr || !buf_addr || buf_size < 2 || buf_size > LISTDIR_BUF_MAX) {
        return (uint64_t)-1;
    }

    char path[EXEC_PATH_MAX];
    size_t i = 0;
    for (; i < EXEC_PATH_MAX; i++) {
        if (copy_from_user(&path[i], (const void*)(path_addr + i), 1) != 0) {
            return (uint64_t)-1;
        }
        if (path[i] == '\0') break;
    }

    if (i == 0 || i == EXEC_PATH_MAX || path[0] != '/') {
        return (uint64_t)-1;
    }

    vfs_node_t* dir = vfs_open(path, 0);
    if (!dir || dir->type != VFS_DIRECTORY) {
        if (dir) vfs_close(dir);
        return (uint64_t)-1;
    }

    char out[LISTDIR_BUF_MAX];
    size_t out_pos = 0;

    for (vfs_node_t* child = dir->first_child; child; child = child->next_sibling) {
        for (size_t j = 0; child->name[j] != '\0'; j++) {
            if (out_pos + 2 >= buf_size || out_pos + 2 >= LISTDIR_BUF_MAX) {
                goto done;
            }
            out[out_pos++] = child->name[j];
        }

        if (child->type == VFS_DIRECTORY) {
            if (out_pos + 2 >= buf_size || out_pos + 2 >= LISTDIR_BUF_MAX) {
                goto done;
            }
            out[out_pos++] = '/';
        }

        if (out_pos + 1 >= buf_size || out_pos + 1 >= LISTDIR_BUF_MAX) {
            goto done;
        }
        out[out_pos++] = '\n';
    }

    if (out_pos == 0 && buf_size >= 9) {
        out[out_pos++] = '(';
        out[out_pos++] = 'e';
        out[out_pos++] = 'm';
        out[out_pos++] = 'p';
        out[out_pos++] = 't';
        out[out_pos++] = 'y';
        out[out_pos++] = ')';
        out[out_pos++] = '\n';
    }

done:
    out[out_pos] = '\0';
    vfs_close(dir);

    if (copy_to_user((void*)buf_addr, out, out_pos + 1) != 0) {
        return (uint64_t)-1;
    }
    return out_pos;
}

static uint64_t sys_fb_putpixel(uint64_t x, uint64_t y, uint64_t color) {
    if (!framebuffer_available()) return -1;
    fb_color_t c = {
        .r = (color >> 16) & 0xFF,
        .g = (color >> 8) & 0xFF,
        .b = color & 0xFF,
        .a = 255
    };
    fb_putpixel((uint32_t)x, (uint32_t)y, c);
    return 0;
}

static uint64_t sys_fb_clear(uint64_t color) {
    if (!framebuffer_available()) return -1;
    fb_color_t c = {
        .r = (color >> 16) & 0xFF,
        .g = (color >> 8) & 0xFF,
        .b = color & 0xFF,
        .a = 255
    };
    fb_clear(c);
    return 0;
}

static uint64_t sys_fb_rect(uint64_t x, uint64_t y, uint64_t w, uint64_t h, uint64_t color) {
    if (!framebuffer_available()) return -1;
    fb_color_t c = {
        .r = (color >> 16) & 0xFF,
        .g = (color >> 8) & 0xFF,
        .b = color & 0xFF,
        .a = 255
    };
    fb_fill_rect((uint32_t)x, (uint32_t)y, (uint32_t)w, (uint32_t)h, c);
    return 0;
}

static uint64_t sys_fb_info(uint64_t info_addr) {
    if (!framebuffer_available()) return -1;
    framebuffer_info_t* fb = framebuffer_get_info();
    struct fb_info info = {
        .width = fb->width,
        .height = fb->height,
        .bpp = fb->bpp
    };
    if (copy_to_user((void*)info_addr, &info, sizeof(info)) != 0) return -1;
    return 0;
}

static uint64_t sys_fb_putchar_syscall(uint64_t x, uint64_t y, uint64_t c, uint64_t fg, uint64_t bg) {
    if (!framebuffer_available()) return -1;
    
    fb_color_t fg_color = {
        .r = (fg >> 16) & 0xFF,
        .g = (fg >> 8) & 0xFF,
        .b = fg & 0xFF,
        .a = 255
    };
    
    fb_color_t bg_color = {
        .r = (bg >> 16) & 0xFF,
        .g = (bg >> 8) & 0xFF,
        .b = bg & 0xFF,
        .a = 255
    };
    
    fb_putchar((uint32_t)x, (uint32_t)y, (char)c, fg_color, bg_color);
    return 0;
}

static uint64_t sys_fb_putchar_psf_syscall(uint64_t x, uint64_t y, uint64_t c, uint64_t fg, uint64_t bg) {
    if (!framebuffer_available()) return -1;
    
    fb_color_t fg_color = {
        .r = (fg >> 16) & 0xFF,
        .g = (fg >> 8) & 0xFF,
        .b = fg & 0xFF,
        .a = 255
    };
    
    fb_color_t bg_color = {
        .r = (bg >> 16) & 0xFF,
        .g = (bg >> 8) & 0xFF,
        .b = bg & 0xFF,
        .a = 255
    };
    
    psf_draw_char((uint32_t)x, (uint32_t)y, (char)c, fg_color, bg_color);
    return 0;
}

void syscall_handler(syscall_regs_t* regs) {
    task_t* current_task = get_current_task();
    if (!current_task) {
        regs->rax = (uint64_t)-1;
        return;
    }
    
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
        case SYSCALL_EXEC:   regs->rax = sys_exec(regs->rdi); break;
        case SYSCALL_FB_PUTPIXEL: regs->rax = sys_fb_putpixel(regs->rdi, regs->rsi, regs->rdx); break;
        case SYSCALL_FB_CLEAR:    regs->rax = sys_fb_clear(regs->rdi); break;
        case SYSCALL_FB_RECT:     regs->rax = sys_fb_rect(regs->rdi, regs->rsi, regs->rdx, regs->r10, regs->r8); break;
        case SYSCALL_FB_INFO:     regs->rax = sys_fb_info(regs->rdi); break;
        case SYSCALL_FB_PUTCHAR:  regs->rax = sys_fb_putchar_syscall(regs->rdi, regs->rsi, regs->rdx, regs->r10, regs->r8); break;
        case SYSCALL_FB_PUTCHAR_PSF: regs->rax = sys_fb_putchar_psf_syscall(regs->rdi, regs->rsi, regs->rdx, regs->r10, regs->r8); break;
        case SYSCALL_LISTDIR: regs->rax = sys_listdir(regs->rdi, regs->rsi, regs->rdx); break;
        default:
            serial_write("[SYSCALL] Unknown syscall: ");
            serial_write_dec(regs->rax);
            serial_write("\n");
            regs->rax = -1;
            break;
    }
}

bool syscall_init(void) {
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
    return true;
}
