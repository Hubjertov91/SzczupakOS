#include <kernel/stdint.h>vvv
#include <kernel/vmm.h>
#include <kernel/task.h>
#include <kernel/serial.h>
#include <kernel/uaccess.h>

#define USER_SPACE_START 0x400000
#define USER_SPACE_END   0x800000000000ULL

static bool validate_user_ptr(const void* ptr, size_t size) {
    uint64_t addr = (uint64_t)ptr;
    if (addr == 0) {
        serial_write("[UACCESS] NULL pointer\n");
        return false;
    }
    if (addr < USER_SPACE_START) {
        serial_write("[UACCESS] Address below user space\n");
        return false;
    }
    if (addr >= USER_SPACE_END) {
        serial_write("[UACCESS] Address above user space\n");
        return false;
    }
    if (addr + size < addr) {
        serial_write("[UACCESS] Integer overflow\n");
        return false;
    }
    if (addr + size > USER_SPACE_END) {
        serial_write("[UACCESS] Access beyond user space\n");
        return false;
    }
    return true;
}

static page_directory_t* get_user_dir(void) {
    task_t* current = task_get_current();
    if (!current) {
        serial_write("[UACCESS] No current task\n");
        return NULL;
    }
    if (current->is_kernel) {
        serial_write("[UACCESS] Current task is kernel task\n");
        return NULL;
    }
    if (!current->page_dir) {
        serial_write("[UACCESS] No user page directory\n");
        return NULL;
    }
    return current->page_dir;
}

bool copy_from_user(void* kernel_dst, const void* user_src, size_t size) {
    if (!kernel_dst || !user_src || size == 0) return false;
    if (!validate_user_ptr(user_src, size)) return false;
    page_directory_t* dir = get_user_dir();
    if (!dir) return false;
    const uint8_t* src = (const uint8_t*)user_src;
    uint8_t* dst = (uint8_t*)kernel_dst;
    for (size_t i = 0; i < size; i++) {
        uint64_t user_virt = (uint64_t)(src + i);
        uint64_t phys = vmm_get_physical(dir, user_virt);
        if (!phys) {
            serial_write("[UACCESS] copy_from_user failed at offset ");
            serial_write_dec(i);
            serial_write(" (virt=0x");
            serial_write_hex(user_virt);
            serial_write(")\n");
            return false;
        }
        dst[i] = *((volatile uint8_t*)phys);
    }
    return true;
}

bool copy_to_user(void* user_dst, const void* kernel_src, size_t size) {
    if (!user_dst || !kernel_src || size == 0) return false;
    if (!validate_user_ptr(user_dst, size)) return false;
    page_directory_t* dir = get_user_dir();
    if (!dir) return false;
    const uint8_t* src = (const uint8_t*)kernel_src;
    for (size_t i = 0; i < size; i++) {
        uint64_t user_virt = (uint64_t)((uint8_t*)user_dst + i);
        uint64_t phys = vmm_get_physical(dir, user_virt);
        if (!phys) {
            serial_write("[UACCESS] copy_to_user failed at offset ");
            serial_write_dec(i);
            serial_write("\n");
            return false;
        }
        *((volatile uint8_t*)phys) = src[i];
    }
    return true;
}

ssize_t strnlen_user(const char* user_str, size_t max_len) {
    if (!user_str || !validate_user_ptr(user_str, 1)) return -1;
    page_directory_t* dir = get_user_dir();
    if (!dir) return -1;
    for (size_t i = 0; i < max_len; i++) {
        uint64_t user_virt = (uint64_t)((const uint8_t*)user_str + i);
        if (user_virt >= USER_SPACE_END) return -1;
        uint64_t phys = vmm_get_physical(dir, user_virt);
        if (!phys) return -1;
        volatile char c = *((volatile char*)phys);
        if (c == '\0') return i;
    }
    return max_len;
}