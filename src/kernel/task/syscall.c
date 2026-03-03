#include <task/syscall.h>
#include <arch/idt.h>
#include <drivers/serial.h>
#include <task/task.h>
#include <mm/uaccess.h>
#include <mm/heap.h>
#include <mm/vmm.h>
#include <kernel/terminal.h>
#include <fs/vfs.h>
#include <drivers/framebuffer.h>
#include <drivers/psf.h>
#include <drivers/keyboard.h>
#include <drivers/mouse.h>
#include <drivers/pci.h>
#include <drivers/usb.h>
#include <net/net.h>
#include <kernel/string.h>
#include <task/scheduler.h>
#include <kernel/pty.h>
#include <arch/api.h>

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
#define EXEC_CMDLINE_MAX 512
#define EXEC_FILE_MAX (2 * 1024 * 1024)
#define LISTDIR_BUF_MAX 4096
#define NET_HOSTNAME_MAX 256
#define NET_HTTP_BODY_MAX 8192
#define FS_IO_BUF_MAX 4096
#define FS_RET_INVALID   ((uint64_t)-1)
#define FS_RET_PARENT    ((uint64_t)-2)
#define FS_RET_EXISTS    ((uint64_t)-3)
#define FS_RET_UNSUP     ((uint64_t)-4)
#define FS_RET_CREATE    ((uint64_t)-5)
#define FS_RET_NOTFOUND  ((uint64_t)-6)
#define FS_RET_DELETE    ((uint64_t)-7)

static bool is_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static char upper_ascii(char c) {
    if (c >= 'a' && c <= 'z') return (char)(c - ('a' - 'A'));
    return c;
}

static bool streq_ascii_nocase(const char* a, const char* b) {
    if (!a || !b) return false;
    while (*a && *b) {
        if (upper_ascii(*a) != upper_ascii(*b)) return false;
        a++;
        b++;
    }
    return *a == *b;
}

static vfs_node_t* vfs_find_child_nocase(vfs_node_t* parent, const char* name) {
    if (!parent || !name || parent->type != VFS_DIRECTORY) return NULL;
    for (vfs_node_t* child = parent->first_child; child; child = child->next_sibling) {
        if (streq_ascii_nocase(child->name, name)) {
            return child;
        }
    }
    return NULL;
}

static int copy_user_string(uint64_t user_addr, char* out, size_t out_size) {
    if (!user_addr || !out || out_size < 2) return -1;

    size_t i = 0;
    for (; i < out_size; i++) {
        if (copy_from_user(&out[i], (const void*)(user_addr + i), 1) != 0) {
            return -1;
        }
        if (out[i] == '\0') break;
    }

    if (i == 0 || i == out_size) return -1;
    return 0;
}

static int split_parent_name(const char* abs_path, char* parent_out, size_t parent_size, char* name_out, size_t name_size) {
    if (!abs_path || !parent_out || !name_out || parent_size < 2 || name_size < 2) {
        return -1;
    }

    size_t len = strlen(abs_path);
    if (len < 2 || abs_path[0] != '/') return -1;

    while (len > 1 && abs_path[len - 1] == '/') {
        len--;
    }
    if (len <= 1) return -1;

    for (size_t i = 1; i < len; i++) {
        if (abs_path[i] == '/' && abs_path[i - 1] == '/') {
            return -1;
        }
    }

    size_t slash = len - 1;
    while (slash > 0 && abs_path[slash] != '/') {
        slash--;
    }
    if (abs_path[slash] != '/') return -1;

    size_t name_len = len - slash - 1;
    if (name_len == 0 || name_len >= name_size) return -1;

    if ((name_len == 1 && abs_path[slash + 1] == '.') ||
        (name_len == 2 && abs_path[slash + 1] == '.' && abs_path[slash + 2] == '.')) {
        return -1;
    }

    if (slash == 0) {
        parent_out[0] = '/';
        parent_out[1] = '\0';
    } else {
        if (slash >= parent_size) return -1;
        memcpy(parent_out, abs_path, slash);
        parent_out[slash] = '\0';
    }

    memcpy(name_out, abs_path + slash + 1, name_len);
    name_out[name_len] = '\0';
    return 0;
}

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
    if (size > 4096) size = 4096;

    if (size == 1) {
        char c = 0;
        if (terminal_read(&c, 1) <= 0) return 0;
        if (c == '\r') c = '\n';
        if (copy_to_user((void*)buf_addr, &c, 1) != 0) return -1;
        return 1;
    }

    char buffer[4096];
    size_t pos = 0;
    
    while (pos < size - 1 && pos < sizeof(buffer) - 1) {
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
        arch_idle_once();
        net_poll();
    }
    return 0;
}

static uint64_t sys_sysinfo(uint64_t info_addr) {
    if (!info_addr) return -1;
    struct sysinfo info;
    info.uptime = pit_get_ticks() / 100;
    info.total_memory = pmm_get_total_memory();
    info.free_memory = pmm_get_total_memory() - pmm_get_used_memory();
    info.nr_processes = task_get_process_count();
    if (copy_to_user((void*)info_addr, &info, sizeof(info)) != 0) return -1;
    return 0;
}

static uint64_t sys_fork(void) {
    task_t* child = task_fork();
    if (!child) return -1;
    return (uint64_t)child->pid;
}

static uint64_t sys_waitpid(uint64_t pid, uint64_t status_addr) {
    int64_t pid_signed = (int64_t)pid;
    if (pid_signed < -1 || pid_signed > 0x7FFFFFFFLL) {
        return (uint64_t)-1;
    }

    int32_t exit_code = 0;
    int32_t waited_pid = task_wait_pid((int32_t)pid_signed, &exit_code);
    if (waited_pid < 0) {
        return (uint64_t)-1;
    }

    if (status_addr != 0u) {
        if (copy_to_user((void*)status_addr, &exit_code, sizeof(exit_code)) != 0) {
            return (uint64_t)-1;
        }
    }

    return (uint64_t)(uint32_t)waited_pid;
}

static bool copy_cmdline_from_user(uint64_t cmdline_addr, char* out, size_t out_size) {
    if (!cmdline_addr || !out || out_size < 2) return false;
    char cmdline[EXEC_CMDLINE_MAX];
    size_t i = 0;
    for (; i < out_size; i++) {
        if (copy_from_user(&cmdline[i], (const void*)(cmdline_addr + i), 1) != 0) {
            return false;
        }
        if (cmdline[i] == '\0') break;
    }
    if (i == 0 || i == out_size) return false;
    memcpy(out, cmdline, i + 1);
    return true;
}

static task_t* spawn_task_from_cmdline(char* cmdline, int32_t pty_id) {
    if (!cmdline) return NULL;

    size_t p = 0;
    while (is_space(cmdline[p])) p++;
    if (cmdline[p] == '\0') return NULL;

    char path[EXEC_PATH_MAX];
    size_t path_len = 0;
    while (cmdline[p] != '\0' && !is_space(cmdline[p])) {
        if (path_len + 1 >= EXEC_PATH_MAX) return NULL;
        path[path_len++] = cmdline[p++];
    }
    path[path_len] = '\0';

    if (path[0] != '/') return NULL;
    vfs_node_t* file = vfs_open(path, 0);
    if (!file || file->type != VFS_FILE || file->size == 0 || file->size > EXEC_FILE_MAX) {
        if (file) vfs_close(file);
        return NULL;
    }

    uint8_t* elf_data = (uint8_t*)kmalloc(file->size);
    if (!elf_data) {
        vfs_close(file);
        return NULL;
    }

    if (!vfs_read(file, elf_data, 0, file->size)) {
        kfree(elf_data);
        vfs_close(file);
        return NULL;
    }

    task_t* task = task_create_user(path, cmdline, elf_data, file->size);
    kfree(elf_data);
    vfs_close(file);

    if (!task) return NULL;

    if (pty_id >= 0) {
        if (!pty_is_open(pty_id) || pty_attach_slave(pty_id, task->pid) != 0) {
            task->state = TASK_TERMINATED;
            task->reap_blocked = false;
            task->parent_pid = 0;
            scheduler_remove_task(task);
            task_reap_terminated();
            return NULL;
        }
        task->pty_id = pty_id;
    }

    return task;
}

static uint64_t sys_exec(uint64_t cmdline_addr) {
    if (!cmdline_addr) return (uint64_t)-1;

    char cmdline[EXEC_CMDLINE_MAX];
    if (!copy_cmdline_from_user(cmdline_addr, cmdline, sizeof(cmdline))) {
        return (uint64_t)-1;
    }

    task_t* current = get_current_task();
    int32_t inherited_pty = (current) ? current->pty_id : -1;
    task_t* task = spawn_task_from_cmdline(cmdline, inherited_pty);
    if (!task) {
        return (uint64_t)-1;
    }

    int32_t waited_pid = task_wait_pid((int32_t)task->pid, NULL);
    if (waited_pid < 0) {
        return (uint64_t)-1;
    }
    return (uint64_t)(uint32_t)waited_pid;
}

static uint64_t sys_pty_open(void) {
    return (uint64_t)pty_open();
}

static uint64_t sys_pty_close(uint64_t pty_id) {
    if (!pty_is_open((int32_t)pty_id)) return (uint64_t)-1;
    return (uint64_t)pty_close((int32_t)pty_id);
}

static uint64_t sys_pty_read(uint64_t pty_id, uint64_t buf_addr, uint64_t size) {
    if (!buf_addr || size == 0) return 0;
    if (!pty_is_open((int32_t)pty_id)) return (uint64_t)-1;
    if (size > 4096) size = 4096;

    char buffer[4096];
    size_t n = pty_host_read((int32_t)pty_id, buffer, (size_t)size);
    if (n == 0) return 0;
    if (copy_to_user((void*)buf_addr, buffer, n) != 0) return (uint64_t)-1;
    return (uint64_t)n;
}

static uint64_t sys_pty_write(uint64_t pty_id, uint64_t buf_addr, uint64_t size) {
    if (!buf_addr || size == 0) return 0;
    if (!pty_is_open((int32_t)pty_id)) return (uint64_t)-1;
    if (size > 4096) size = 4096;

    char buffer[4096];
    if (copy_from_user(buffer, (const void*)buf_addr, size) != 0) return (uint64_t)-1;
    return (uint64_t)pty_host_write((int32_t)pty_id, buffer, (size_t)size);
}

static uint64_t sys_pty_spawn(uint64_t cmdline_addr, uint64_t pty_id) {
    if (!cmdline_addr) return (uint64_t)-1;
    if (!pty_is_open((int32_t)pty_id)) return (uint64_t)-1;

    char cmdline[EXEC_CMDLINE_MAX];
    if (!copy_cmdline_from_user(cmdline_addr, cmdline, sizeof(cmdline))) {
        return (uint64_t)-1;
    }

    task_t* task = spawn_task_from_cmdline(cmdline, (int32_t)pty_id);
    if (!task) return (uint64_t)-1;
    return (uint64_t)task->pid;
}

static uint64_t sys_pty_out_avail(uint64_t pty_id) {
    if (!pty_is_open((int32_t)pty_id)) return (uint64_t)-1;
    return (uint64_t)pty_host_out_available((int32_t)pty_id);
}

static uint64_t sys_pty_in_avail(uint64_t pty_id) {
    if (!pty_is_open((int32_t)pty_id)) return (uint64_t)-1;
    return (uint64_t)pty_host_in_available((int32_t)pty_id);
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

static uint64_t sys_fs_touch(uint64_t path_addr) {
    char path[EXEC_PATH_MAX];
    if (copy_user_string(path_addr, path, sizeof(path)) != 0) {
        return FS_RET_INVALID;
    }

    char parent_path[EXEC_PATH_MAX];
    char name[MAX_FILENAME];
    if (split_parent_name(path, parent_path, sizeof(parent_path), name, sizeof(name)) != 0) {
        return FS_RET_INVALID;
    }

    vfs_node_t* parent = vfs_open(parent_path, 0);
    if (!parent || parent->type != VFS_DIRECTORY) {
        if (parent) vfs_close(parent);
        return FS_RET_PARENT;
    }

    vfs_node_t* existing = vfs_find_child_nocase(parent, name);
    if (existing) {
        uint64_t rc = (existing->type == VFS_FILE) ? 0 : FS_RET_EXISTS;
        vfs_close(parent);
        return rc;
    }

    vfs_node_t* file = vfs_create_file(parent, name);
    vfs_close(parent);
    if (!file) {
        return FS_RET_CREATE;
    }
    return 0;
}

static uint64_t sys_fs_mkdir(uint64_t path_addr) {
    char path[EXEC_PATH_MAX];
    if (copy_user_string(path_addr, path, sizeof(path)) != 0) {
        return FS_RET_INVALID;
    }

    char parent_path[EXEC_PATH_MAX];
    char name[MAX_FILENAME];
    if (split_parent_name(path, parent_path, sizeof(parent_path), name, sizeof(name)) != 0) {
        return FS_RET_INVALID;
    }

    vfs_node_t* parent = vfs_open(parent_path, 0);
    if (!parent || parent->type != VFS_DIRECTORY) {
        if (parent) vfs_close(parent);
        return FS_RET_PARENT;
    }

    if (vfs_find_child_nocase(parent, name)) {
        vfs_close(parent);
        return FS_RET_EXISTS;
    }

    vfs_node_t* dir = vfs_create_directory(parent, name);
    vfs_close(parent);
    if (!dir) return FS_RET_CREATE;
    return 0;
}

static uint64_t sys_fs_delete(uint64_t path_addr) {
    char path[EXEC_PATH_MAX];
    if (copy_user_string(path_addr, path, sizeof(path)) != 0) {
        return FS_RET_INVALID;
    }

    char parent_path[EXEC_PATH_MAX];
    char name[MAX_FILENAME];
    if (split_parent_name(path, parent_path, sizeof(parent_path), name, sizeof(name)) != 0) {
        return FS_RET_INVALID;
    }

    vfs_node_t* parent = vfs_open(parent_path, 0);
    if (!parent || parent->type != VFS_DIRECTORY) {
        if (parent) vfs_close(parent);
        return FS_RET_PARENT;
    }

    vfs_node_t* node = vfs_find_child_nocase(parent, name);
    if (!node) {
        vfs_close(parent);
        return FS_RET_NOTFOUND;
    }

    bool ok = vfs_delete(node);
    vfs_close(parent);
    if (!ok) return FS_RET_DELETE;
    return 0;
}

static vfs_node_t* open_or_create_file_for_write(const char* path) {
    if (!path) return NULL;

    vfs_node_t* node = vfs_open(path, 0);
    if (node) {
        if (node->type != VFS_FILE) {
            vfs_close(node);
            return NULL;
        }
        return node;
    }

    char parent_path[EXEC_PATH_MAX];
    char name[MAX_FILENAME];
    if (split_parent_name(path, parent_path, sizeof(parent_path), name, sizeof(name)) != 0) {
        return NULL;
    }

    vfs_node_t* parent = vfs_open(parent_path, 0);
    if (!parent || parent->type != VFS_DIRECTORY) {
        if (parent) vfs_close(parent);
        return NULL;
    }

    node = vfs_find_child_nocase(parent, name);
    if (node) {
        if (node->type != VFS_FILE) {
            vfs_close(parent);
            return NULL;
        }
        vfs_close(parent);
        return node;
    }

    node = vfs_create_file(parent, name);
    vfs_close(parent);
    if (!node || node->type != VFS_FILE) {
        return NULL;
    }
    return node;
}

static uint64_t sys_fs_read(uint64_t path_addr, uint64_t offset, uint64_t buf_addr, uint64_t size) {
    if (!path_addr || !buf_addr) return (uint64_t)-1;
    if (size == 0) return 0;
    if (offset > (uint64_t)(~(size_t)0)) return (uint64_t)-1;
    if (size > FS_IO_BUF_MAX) size = FS_IO_BUF_MAX;

    char path[EXEC_PATH_MAX];
    if (copy_user_string(path_addr, path, sizeof(path)) != 0) {
        return (uint64_t)-1;
    }

    vfs_node_t* file = vfs_open(path, 0);
    if (!file || file->type != VFS_FILE) {
        if (file) vfs_close(file);
        return (uint64_t)-1;
    }

    size_t off = (size_t)offset;
    if (off >= file->size) {
        vfs_close(file);
        return 0;
    }

    size_t to_read = (size_t)size;
    size_t remaining = file->size - off;
    if (to_read > remaining) {
        to_read = remaining;
    }

    uint8_t io_buf[FS_IO_BUF_MAX];
    if (!vfs_read(file, io_buf, off, to_read)) {
        vfs_close(file);
        return (uint64_t)-1;
    }
    vfs_close(file);

    if (copy_to_user((void*)buf_addr, io_buf, to_read) != 0) {
        return (uint64_t)-1;
    }
    return (uint64_t)to_read;
}

static uint64_t sys_fs_write(uint64_t path_addr, uint64_t offset, uint64_t buf_addr, uint64_t size) {
    if (!path_addr) return (uint64_t)-1;
    if (size == 0) return 0;
    if (!buf_addr) return (uint64_t)-1;
    if (offset > (uint64_t)(~(size_t)0)) return (uint64_t)-1;
    if (size > FS_IO_BUF_MAX) size = FS_IO_BUF_MAX;

    char path[EXEC_PATH_MAX];
    if (copy_user_string(path_addr, path, sizeof(path)) != 0) {
        return (uint64_t)-1;
    }

    uint8_t io_buf[FS_IO_BUF_MAX];
    if (copy_from_user(io_buf, (const void*)buf_addr, size) != 0) {
        return (uint64_t)-1;
    }

    vfs_node_t* file = open_or_create_file_for_write(path);
    if (!file) {
        return (uint64_t)-1;
    }

    bool ok = vfs_write(file, io_buf, (size_t)offset, (size_t)size);
    vfs_close(file);
    if (!ok) return (uint64_t)-1;
    return size;
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

static uint64_t sys_fb_getpixel(uint64_t x, uint64_t y) {
    if (!framebuffer_available()) return (uint64_t)-1;
    uint32_t color = 0;
    if (!fb_getpixel_rgb((uint32_t)x, (uint32_t)y, &color)) {
        return (uint64_t)-1;
    }
    return (uint64_t)color;
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
        .bpp = fb->bpp,
        .font_width = psf_get_width(),
        .font_height = psf_get_height()
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

static uint64_t sys_kb_poll(void) {
    usb_poll();
    if (!keyboard_has_input()) return 0;
    return (uint64_t)(uint8_t)keyboard_getchar();
}

static uint64_t sys_mouse_poll(uint64_t event_addr) {
    if (!event_addr) return (uint64_t)-1;

    usb_poll();

    mouse_event_t ev;
    if (!mouse_poll_event(&ev)) return 0;

    struct mouse_event out = {
        .x = ev.x,
        .y = ev.y,
        .dx = ev.dx,
        .dy = ev.dy,
        .buttons = ev.buttons,
        .changed = ev.changed,
        ._reserved = 0,
        .seq = ev.seq
    };

    if (copy_to_user((void*)event_addr, &out, sizeof(out)) != 0) {
        return (uint64_t)-1;
    }
    return 1;
}

static uint64_t sys_net_info(uint64_t info_addr) {
    if (!info_addr) return (uint64_t)-1;

    net_info_t kinfo;
    if (!net_get_info(&kinfo)) return (uint64_t)-1;

    struct net_info uinfo;
    memset(&uinfo, 0, sizeof(uinfo));
    uinfo.link_up = kinfo.link_up ? 1 : 0;
    uinfo.configured = kinfo.configured ? 1 : 0;
    memcpy(uinfo.mac, kinfo.mac, 6);
    memcpy(uinfo.ip, kinfo.ip, 4);
    memcpy(uinfo.netmask, kinfo.netmask, 4);
    memcpy(uinfo.gateway, kinfo.gateway, 4);
    memcpy(uinfo.dns, kinfo.dns, 4);
    uinfo.lease_time_seconds = kinfo.lease_time_seconds;

    if (copy_to_user((void*)info_addr, &uinfo, sizeof(uinfo)) != 0) return (uint64_t)-1;
    return 0;
}

static uint64_t sys_net_ping(uint64_t ip_addr, uint64_t timeout_ms, uint64_t rtt_addr) {
    if (!ip_addr) return (uint64_t)-1;

    uint8_t ip[4];
    if (copy_from_user(ip, (const void*)ip_addr, sizeof(ip)) != 0) return (uint64_t)-1;

    uint32_t rtt_ms = 0;
    if (!net_ping_ipv4(ip, (uint32_t)timeout_ms, &rtt_ms)) return (uint64_t)-1;

    if (rtt_addr) {
        if (copy_to_user((void*)rtt_addr, &rtt_ms, sizeof(rtt_ms)) != 0) return (uint64_t)-1;
    }
    return 0;
}

static uint64_t sys_net_resolve(uint64_t hostname_addr, uint64_t timeout_ms, uint64_t out_ip_addr) {
    if (!hostname_addr || !out_ip_addr) {
        return (uint64_t)-1;
    }

    char hostname[NET_HOSTNAME_MAX];
    size_t i = 0;
    for (; i < NET_HOSTNAME_MAX; i++) {
        if (copy_from_user(&hostname[i], (const void*)(hostname_addr + i), 1) != 0) {
            return (uint64_t)-1;
        }
        if (hostname[i] == '\0') {
            break;
        }
    }

    if (i == 0 || i == NET_HOSTNAME_MAX || hostname[0] == '\0') {
        return (uint64_t)-1;
    }

    uint8_t ip[4];
    if (!net_dns_resolve_ipv4(hostname, ip, (uint32_t)timeout_ms)) {
        return (uint64_t)-1;
    }

    if (copy_to_user((void*)out_ip_addr, ip, sizeof(ip)) != 0) {
        return (uint64_t)-1;
    }

    return 0;
}

static uint64_t sys_net_stats(uint64_t stats_addr) {
    if (!stats_addr) return (uint64_t)-1;

    net_stats_t kstats;
    if (!net_get_stats(&kstats)) return (uint64_t)-1;

    struct net_stats ustats;
    memset(&ustats, 0, sizeof(ustats));
    ustats.rx_frames = kstats.rx_frames;
    ustats.tx_frames = kstats.tx_frames;
    ustats.rx_ipv4 = kstats.rx_ipv4;
    ustats.rx_arp = kstats.rx_arp;
    ustats.rx_icmp = kstats.rx_icmp;
    ustats.rx_udp = kstats.rx_udp;
    ustats.rx_tcp = kstats.rx_tcp;
    ustats.rx_dhcp = kstats.rx_dhcp;
    ustats.rx_dns = kstats.rx_dns;
    ustats.tx_ipv4 = kstats.tx_ipv4;
    ustats.tx_arp = kstats.tx_arp;
    ustats.tx_icmp = kstats.tx_icmp;
    ustats.tx_udp = kstats.tx_udp;
    ustats.arp_cache_hits = kstats.arp_cache_hits;
    ustats.arp_cache_misses = kstats.arp_cache_misses;
    ustats.dns_cache_hits = kstats.dns_cache_hits;
    ustats.dns_cache_misses = kstats.dns_cache_misses;
    ustats.dns_queries = kstats.dns_queries;
    ustats.dns_timeouts = kstats.dns_timeouts;
    ustats.arp_cache_entries = kstats.arp_cache_entries;
    ustats.dns_cache_entries = kstats.dns_cache_entries;

    if (copy_to_user((void*)stats_addr, &ustats, sizeof(ustats)) != 0) return (uint64_t)-1;
    return 0;
}

static uint64_t sys_net_trace_probe(uint64_t req_addr, uint64_t rsp_addr) {
    if (!req_addr || !rsp_addr) return (uint64_t)-1;

    struct net_trace_probe_req req;
    if (copy_from_user(&req, (const void*)req_addr, sizeof(req)) != 0) {
        return (uint64_t)-1;
    }

    struct net_trace_probe_rsp rsp;
    memset(&rsp, 0, sizeof(rsp));

    uint8_t hop_ip[4] = {0, 0, 0, 0};
    uint32_t rtt_ms = 0;
    bool reached_dst = false;
    uint8_t ttl = (req.ttl == 0) ? 1u : req.ttl;

    bool ok = net_trace_probe_ipv4(req.dst_ip, ttl, req.timeout_ms, &rtt_ms, hop_ip, &reached_dst);
    rsp.ok = ok ? 1u : 0u;
    rsp.reached_dst = reached_dst ? 1u : 0u;
    memcpy(rsp.hop_ip, hop_ip, sizeof(rsp.hop_ip));
    rsp.rtt_ms = rtt_ms;

    if (copy_to_user((void*)rsp_addr, &rsp, sizeof(rsp)) != 0) {
        return (uint64_t)-1;
    }

    return 0;
}

static uint64_t sys_net_tcp_probe(uint64_t req_addr, uint64_t rsp_addr) {
    if (!req_addr || !rsp_addr) return (uint64_t)-1;

    struct net_tcp_probe_req req;
    if (copy_from_user(&req, (const void*)req_addr, sizeof(req)) != 0) {
        return (uint64_t)-1;
    }

    struct net_tcp_probe_rsp rsp;
    memset(&rsp, 0, sizeof(rsp));

    bool open = false;
    uint32_t rtt_ms = 0;
    bool ok = net_tcp_probe_ipv4(req.dst_ip, req.dst_port, req.timeout_ms, &open, &rtt_ms);
    rsp.ok = ok ? 1u : 0u;
    rsp.open = open ? 1u : 0u;
    rsp.rtt_ms = rtt_ms;

    if (copy_to_user((void*)rsp_addr, &rsp, sizeof(rsp)) != 0) {
        return (uint64_t)-1;
    }

    return 0;
}

static bool fixed_buffer_has_nul(const char* buf, size_t max_len) {
    if (!buf || max_len == 0) {
        return false;
    }
    for (size_t i = 0; i < max_len; i++) {
        if (buf[i] == '\0') {
            return true;
        }
    }
    return false;
}

static uint64_t sys_net_http_get(uint64_t req_addr, uint64_t rsp_addr) {
    if (!req_addr || !rsp_addr) return (uint64_t)-1;

    struct net_http_get_req req;
    if (copy_from_user(&req, (const void*)req_addr, sizeof(req)) != 0) {
        return (uint64_t)-1;
    }

    if (!req.out_body_addr || req.out_body_capacity == 0 ||
        req.out_body_capacity > NET_HTTP_BODY_MAX) {
        return (uint64_t)-1;
    }

    if (!fixed_buffer_has_nul(req.host, sizeof(req.host)) ||
        !fixed_buffer_has_nul(req.path, sizeof(req.path))) {
        return (uint64_t)-1;
    }

    uint8_t* body = kmalloc(req.out_body_capacity);
    if (!body) {
        return (uint64_t)-1;
    }

    const char* path = (req.path[0] != '\0') ? req.path : "/";
    uint16_t status_code = 0;
    uint32_t body_length = 0;
    bool truncated = false;
    bool ok = net_http_get_ipv4(req.dst_ip, req.dst_port,
                                req.host, path, req.timeout_ms,
                                body, req.out_body_capacity,
                                &body_length, &status_code, &truncated);

    if (ok && body_length > 0) {
        if (copy_to_user((void*)req.out_body_addr, body, body_length) != 0) {
            kfree(body);
            return (uint64_t)-1;
        }
    }

    struct net_http_get_rsp rsp;
    memset(&rsp, 0, sizeof(rsp));
    rsp.ok = ok ? 1u : 0u;
    rsp.truncated = truncated ? 1u : 0u;
    rsp.status_code = status_code;
    rsp.body_length = body_length;

    kfree(body);

    if (copy_to_user((void*)rsp_addr, &rsp, sizeof(rsp)) != 0) {
        return (uint64_t)-1;
    }

    return 0;
}

static uint64_t sys_pci_get_count(void) {
    return (uint64_t)pci_get_device_count();
}

static uint64_t sys_pci_get_device(uint64_t index, uint64_t out_addr) {
    if (!out_addr || index > 0xFFFFu) {
        return (uint64_t)-1;
    }

    pci_device_t kdev;
    if (!pci_get_device((uint16_t)index, &kdev)) {
        return (uint64_t)-1;
    }

    struct pci_device_info udev;
    memset(&udev, 0, sizeof(udev));
    udev.bus = kdev.bus;
    udev.slot = kdev.slot;
    udev.function = kdev.function;
    udev.class_code = kdev.class_code;
    udev.subclass = kdev.subclass;
    udev.prog_if = kdev.prog_if;
    udev.revision_id = kdev.revision_id;
    udev.header_type = kdev.header_type;
    udev.vendor_id = kdev.vendor_id;
    udev.device_id = kdev.device_id;
    udev.is_pcie = kdev.is_pcie ? 1u : 0u;

    if (copy_to_user((void*)out_addr, &udev, sizeof(udev)) != 0) {
        return (uint64_t)-1;
    }
    return 0;
}

static uint64_t sys_usb_get_count(void) {
    return (uint64_t)usb_get_controller_count();
}

static uint64_t sys_usb_get_controller(uint64_t index, uint64_t out_addr) {
    if (!out_addr || index > 0xFFFFu) {
        return (uint64_t)-1;
    }

    usb_controller_info_t kctrl;
    if (!usb_get_controller((uint16_t)index, &kctrl)) {
        return (uint64_t)-1;
    }

    struct usb_controller_info uctrl;
    memset(&uctrl, 0, sizeof(uctrl));
    uctrl.bus = kctrl.bus;
    uctrl.slot = kctrl.slot;
    uctrl.function = kctrl.function;
    uctrl.controller_type = kctrl.controller_type;
    uctrl.class_code = kctrl.class_code;
    uctrl.subclass = kctrl.subclass;
    uctrl.prog_if = kctrl.prog_if;
    uctrl.vendor_id = kctrl.vendor_id;
    uctrl.device_id = kctrl.device_id;
    uctrl.is_pcie = kctrl.is_pcie ? 1u : 0u;
    uctrl.initialized = kctrl.initialized ? 1u : 0u;
    uctrl.port_count = kctrl.port_count;
    uctrl.connected_ports = kctrl.connected_ports;
    uctrl.io_base = kctrl.io_base;
    uctrl.mmio_base = kctrl.mmio_base;

    if (copy_to_user((void*)out_addr, &uctrl, sizeof(uctrl)) != 0) {
        return (uint64_t)-1;
    }
    return 0;
}

void syscall_handler(syscall_regs_t* regs) {
    task_t* current_task = get_current_task();
    if (!current_task) {
        regs->rax = (uint64_t)-1;
        return;
    }

    if (!current_task->is_kernel && current_task->page_dir) {
        vmm_sync_kernel_mappings(current_task->page_dir);
    }
    
    switch (regs->rax) {
        case SYSCALL_EXIT:   task_exit((int32_t)regs->rdi); break;
        case SYSCALL_WRITE:  regs->rax = sys_write(regs->rdi, regs->rsi); break;
        case SYSCALL_READ:   regs->rax = sys_read(regs->rdi, regs->rsi); break;
        case SYSCALL_GETPID: regs->rax = sys_getpid(); break;
        case SYSCALL_GETTIME:regs->rax = sys_gettime(); break;
        case SYSCALL_SLEEP:  regs->rax = sys_sleep(regs->rdi); break;
        case SYSCALL_CLEAR:  terminal_clear(); regs->rax = 0; break;
        case SYSCALL_SYSINFO:regs->rax = sys_sysinfo(regs->rdi); break;
        case SYSCALL_FORK:   regs->rax = sys_fork(); break;
        case SYSCALL_WAITPID:regs->rax = sys_waitpid(regs->rdi, regs->rsi); break;
        case SYSCALL_EXEC:   regs->rax = sys_exec(regs->rdi); break;
        case SYSCALL_FB_PUTPIXEL: regs->rax = sys_fb_putpixel(regs->rdi, regs->rsi, regs->rdx); break;
        case SYSCALL_FB_GETPIXEL: regs->rax = sys_fb_getpixel(regs->rdi, regs->rsi); break;
        case SYSCALL_FB_CLEAR:    regs->rax = sys_fb_clear(regs->rdi); break;
        case SYSCALL_FB_RECT:     regs->rax = sys_fb_rect(regs->rdi, regs->rsi, regs->rdx, regs->r10, regs->r8); break;
        case SYSCALL_FB_INFO:     regs->rax = sys_fb_info(regs->rdi); break;
        case SYSCALL_FB_PUTCHAR:  regs->rax = sys_fb_putchar_syscall(regs->rdi, regs->rsi, regs->rdx, regs->r10, regs->r8); break;
        case SYSCALL_FB_PUTCHAR_PSF: regs->rax = sys_fb_putchar_psf_syscall(regs->rdi, regs->rsi, regs->rdx, regs->r10, regs->r8); break;
        case SYSCALL_LISTDIR: regs->rax = sys_listdir(regs->rdi, regs->rsi, regs->rdx); break;
        case SYSCALL_FS_TOUCH: regs->rax = sys_fs_touch(regs->rdi); break;
        case SYSCALL_FS_MKDIR: regs->rax = sys_fs_mkdir(regs->rdi); break;
        case SYSCALL_FS_DELETE: regs->rax = sys_fs_delete(regs->rdi); break;
        case SYSCALL_FS_READ: regs->rax = sys_fs_read(regs->rdi, regs->rsi, regs->rdx, regs->r10); break;
        case SYSCALL_FS_WRITE: regs->rax = sys_fs_write(regs->rdi, regs->rsi, regs->rdx, regs->r10); break;
        case SYSCALL_NET_INFO: regs->rax = sys_net_info(regs->rdi); break;
        case SYSCALL_NET_PING: regs->rax = sys_net_ping(regs->rdi, regs->rsi, regs->rdx); break;
        case SYSCALL_NET_RESOLVE: regs->rax = sys_net_resolve(regs->rdi, regs->rsi, regs->rdx); break;
        case SYSCALL_NET_STATS: regs->rax = sys_net_stats(regs->rdi); break;
        case SYSCALL_NET_TRACE_PROBE: regs->rax = sys_net_trace_probe(regs->rdi, regs->rsi); break;
        case SYSCALL_NET_TCP_PROBE: regs->rax = sys_net_tcp_probe(regs->rdi, regs->rsi); break;
        case SYSCALL_NET_HTTP_GET: regs->rax = sys_net_http_get(regs->rdi, regs->rsi); break;
        case SYSCALL_PCI_GET_COUNT: regs->rax = sys_pci_get_count(); break;
        case SYSCALL_PCI_GET_DEVICE: regs->rax = sys_pci_get_device(regs->rdi, regs->rsi); break;
        case SYSCALL_USB_GET_COUNT: regs->rax = sys_usb_get_count(); break;
        case SYSCALL_USB_GET_CONTROLLER: regs->rax = sys_usb_get_controller(regs->rdi, regs->rsi); break;
        case SYSCALL_KB_POLL: regs->rax = sys_kb_poll(); break;
        case SYSCALL_MOUSE_POLL: regs->rax = sys_mouse_poll(regs->rdi); break;
        case SYSCALL_PTY_OPEN: regs->rax = sys_pty_open(); break;
        case SYSCALL_PTY_CLOSE: regs->rax = sys_pty_close(regs->rdi); break;
        case SYSCALL_PTY_READ: regs->rax = sys_pty_read(regs->rdi, regs->rsi, regs->rdx); break;
        case SYSCALL_PTY_WRITE: regs->rax = sys_pty_write(regs->rdi, regs->rsi, regs->rdx); break;
        case SYSCALL_PTY_SPAWN: regs->rax = sys_pty_spawn(regs->rdi, regs->rsi); break;
        case SYSCALL_PTY_OUT_AVAIL: regs->rax = sys_pty_out_avail(regs->rdi); break;
        case SYSCALL_PTY_IN_AVAIL: regs->rax = sys_pty_in_avail(regs->rdi); break;
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
