#include <kernel/stdint.h>
#include <kernel/vmm.h>
#include <kernel/task.h>
#include <kernel/serial.h>

static page_directory_t* get_user_dir(void) {
    task_t* current = task_get_current();
    if (!current || current->is_kernel || !current->page_dir) return NULL;
    return current->page_dir;
}

bool copy_from_user(void* kernel_dst, const void* user_src, size_t size) {
    if (!kernel_dst || !user_src || size == 0) return false;
    
    page_directory_t* dir = get_user_dir();
    if (!dir) {
        serial_write("[UACCESS] No user page directory!\n");
        return false;
    }
    
    const uint8_t* src = (const uint8_t*)user_src;
    uint8_t* dst = (uint8_t*)kernel_dst;
    
    serial_write("[UACCESS] copy_from_user: src=0x");
    serial_write_hex((uint64_t)user_src);
    serial_write(" size=");
    serial_write_dec(size);
    serial_write(" dir->pml4_phys=0x");
    serial_write_hex(dir->pml4_phys);
    serial_write("\n");
    
    for (size_t i = 0; i < size; i++) {
        uint64_t user_virt = (uint64_t)(src + i);
        uint64_t phys = vmm_get_physical(dir, user_virt);
        
        if (!phys) {
            serial_write("[UACCESS] Failed at i=");
            serial_write_dec(i);
            serial_write(" virt=0x");
            serial_write_hex(user_virt);
            serial_write("\n");
            return false;
        }
        
        if (i < 8) {
            serial_write("[UACCESS] i=");
            serial_write_dec(i);
            serial_write(" virt=0x");
            serial_write_hex(user_virt);
            serial_write(" phys=0x");
            serial_write_hex(phys);
            serial_write(" byte=0x");
            serial_write_hex(*((volatile uint8_t*)phys));
            serial_write("\n");
        }
        
        dst[i] = *((volatile uint8_t*)phys);
    }
    
    return true;
}

bool copy_to_user(void* user_dst, const void* kernel_src, size_t size) {
    if (!user_dst || !kernel_src || size == 0) return false;
    
    page_directory_t* dir = get_user_dir();
    if (!dir) return false;
    
    const uint8_t* src = (const uint8_t*)kernel_src;
    
    for (size_t i = 0; i < size; i++) {
        uint64_t user_virt = (uint64_t)((uint8_t*)user_dst + i);
        uint64_t phys = vmm_get_physical(dir, user_virt);
        
        if (!phys) return false;
        
        *((volatile uint8_t*)phys) = src[i];
    }
    
    return true;
}

ssize_t strnlen_user(const char* user_str, size_t max_len) {
    if (!user_str) return -1;
    
    page_directory_t* dir = get_user_dir();
    if (!dir) return -1;
    
    for (size_t i = 0; i < max_len; i++) {
        uint64_t user_virt = (uint64_t)((const uint8_t*)user_str + i);
        uint64_t phys = vmm_get_physical(dir, user_virt);
        
        if (!phys) return -1;
        
        volatile char c = *((volatile char*)phys);
        if (c == '\0') return i;
    }
    
    return max_len;
}