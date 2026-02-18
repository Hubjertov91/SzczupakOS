#include <kernel/stdint.h>
#include <mm/vmm.h>
#include <task/task.h>
#include <drivers/serial.h>
#include <mm/uaccess.h>
#include <mm/heap.h>

#define USER_SPACE_START 0x400000
#define USER_SPACE_END   0x800000000000ULL
#define PAGE_SIZE        4096
#define PAGE_MASK        (PAGE_SIZE - 1)
#define MAX_USER_STRING  (16 * 1024 * 1024)
#define FAST_COPY_THRESHOLD 64

#define EFAULT 14
#define EINVAL 22

static bool smap_enabled = false;

static inline bool has_smap(void) {
    uint32_t eax, ebx, ecx, edx;
    __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(7), "c"(0));
    return (ebx & (1 << 20)) != 0;
}

static inline void stac(void) {
    __asm__ volatile("stac" ::: "cc");
}

static inline void clac(void) {
    __asm__ volatile("clac" ::: "cc");
}

void uaccess_init(void) {
    if (has_smap()) {
        smap_enabled = true;
        serial_write("[UACCESS] SMAP detected and enabled\n");
    }
}

static inline int validate_user_range(uint64_t addr, size_t size) {
    if (addr == 0) return -EFAULT;
    if (addr < USER_SPACE_START) return -EFAULT;
    if (addr >= USER_SPACE_END) return -EFAULT;
    if (size > USER_SPACE_END) return -EINVAL;
    if (addr > USER_SPACE_END - size) return -EFAULT;
    return 0;
}

static inline page_directory_t* get_current_user_dir(void) {
    task_t* current = get_current_task();
    if (!current || current->is_kernel || !current->page_dir) {
        return NULL;
    }
    return current->page_dir;
}

static inline bool check_page_permissions(page_directory_t* dir, uint64_t virt, bool write) {
    if (!dir) return false;
    
    uint64_t* pml4 = dir->pml4;
    size_t pml4_idx = (virt >> 39) & 0x1FF;
    if (!(pml4[pml4_idx] & PAGE_PRESENT)) return false;
    if (!(pml4[pml4_idx] & PAGE_USER)) return false;
    if (write && !(pml4[pml4_idx] & PAGE_WRITE)) return false;
    
    uint64_t* pdp = (uint64_t*)((pml4[pml4_idx] & ~0xFFFULL) + 0xFFFF800000000000ULL);
    size_t pdp_idx = (virt >> 30) & 0x1FF;
    if (!(pdp[pdp_idx] & PAGE_PRESENT)) return false;
    if (!(pdp[pdp_idx] & PAGE_USER)) return false;
    if (write && !(pdp[pdp_idx] & PAGE_WRITE)) return false;
    
    if (pdp[pdp_idx] & (1ULL << 7)) return true;
    
    uint64_t* pd = (uint64_t*)((pdp[pdp_idx] & ~0xFFFULL) + 0xFFFF800000000000ULL);
    size_t pd_idx = (virt >> 21) & 0x1FF;
    if (!(pd[pd_idx] & PAGE_PRESENT)) return false;
    if (!(pd[pd_idx] & PAGE_USER)) return false;
    if (write && !(pd[pd_idx] & PAGE_WRITE)) return false;
    
    if (pd[pd_idx] & (1ULL << 7)) return true;
    
    uint64_t* pt = (uint64_t*)((pd[pd_idx] & ~0xFFFULL) + 0xFFFF800000000000ULL);
    size_t pt_idx = (virt >> 12) & 0x1FF;
    if (!(pt[pt_idx] & PAGE_PRESENT)) return false;
    if (!(pt[pt_idx] & PAGE_USER)) return false;
    if (write && !(pt[pt_idx] & PAGE_WRITE)) return false;
    
    return true;
}

static inline void fast_copy_aligned(uint64_t* dst, const uint64_t* src, size_t qwords) {
    for (size_t i = 0; i < qwords; i += 8) {
        __builtin_prefetch(src + i + 64, 0, 0);
        dst[i] = src[i];
        if (i + 1 < qwords) dst[i+1] = src[i+1];
        if (i + 2 < qwords) dst[i+2] = src[i+2];
        if (i + 3 < qwords) dst[i+3] = src[i+3];
        if (i + 4 < qwords) dst[i+4] = src[i+4];
        if (i + 5 < qwords) dst[i+5] = src[i+5];
        if (i + 6 < qwords) dst[i+6] = src[i+6];
        if (i + 7 < qwords) dst[i+7] = src[i+7];
    }
}

int copy_from_user(void* kernel_dst, const void* user_src, size_t size) {
    if (!kernel_dst || !user_src || size == 0) {
        return -EINVAL;
    }
    
    uint64_t src_addr = (uint64_t)user_src;
    
    page_directory_t* dir = get_current_user_dir();
    if (!dir) {
        return -EFAULT;
    }
    
    uint8_t* dst = (uint8_t*)kernel_dst;
    size_t copied = 0;
    
    while (copied < size) {
        uint64_t vaddr = src_addr + copied;
        
        uint64_t phys = vmm_get_physical(dir, vaddr);
        if (!phys) {
            return -EFAULT;
        }
        
        size_t page_offset = vaddr & 0xFFF;
        size_t chunk = 4096 - page_offset;
        if (chunk > size - copied) chunk = size - copied;
        
        const uint8_t* src_page = (const uint8_t*)((phys & ~0xFFFULL) + 0xFFFF800000000000ULL + page_offset);
        
        for (size_t i = 0; i < chunk; i++) {
            dst[copied + i] = src_page[i];
        }
        
        copied += chunk;
    }
    
    return 0;
}

int copy_to_user(void* user_dst, const void* kernel_src, size_t size) {
    if (!user_dst || !kernel_src || size == 0) {
        serial_write("[UACCESS] copy_to_user: invalid params\n");
        return -EINVAL;
    }
    
    uint64_t dst_addr = (uint64_t)user_dst;
    int ret = validate_user_range(dst_addr, size);
    if (ret) {
        serial_write("[UACCESS] copy_to_user: invalid user range ");
        serial_write_hex(dst_addr);
        serial_write("\n");
        return ret;
    }
    
    page_directory_t* dir = get_current_user_dir();
    if (!dir) {
        serial_write("[UACCESS] copy_to_user: no user dir\n");
        return -EFAULT;
    }
    
    const uint8_t* src = (const uint8_t*)kernel_src;
    size_t copied = 0;
    
    while (copied < size) {
        uint64_t vaddr = dst_addr + copied;
        
        uint64_t phys = vmm_get_physical(dir, vaddr);
        if (!phys) {
            serial_write("[UACCESS] copy_to_user: no mapping at ");
            serial_write_hex(vaddr);
            serial_write("\n");
            return -EFAULT;
        }
        
        if (!check_page_permissions(dir, vaddr, true)) {
            serial_write("[UACCESS] copy_to_user: no permissions at ");
            serial_write_hex(vaddr);
            serial_write("\n");
            return -EFAULT;
        }
        
        size_t page_offset = vaddr & 0xFFF;
        size_t chunk = 4096 - page_offset;
        if (chunk > size - copied) chunk = size - copied;
        
        uint8_t* dst_page = (uint8_t*)((phys & ~0xFFFULL) + 0xFFFF800000000000ULL + page_offset);
        
        for (size_t i = 0; i < chunk; i++) {
            dst_page[i] = src[copied + i];
        }
        
        copied += chunk;
    }
    
    return 0;
}

ssize_t strnlen_user(const char* user_str, size_t max_len) {
    if (!user_str) return -EFAULT;
    if (max_len > MAX_USER_STRING) max_len = MAX_USER_STRING;
    
    uint64_t str_addr = (uint64_t)user_str;
    int ret = validate_user_range(str_addr, 1);
    if (ret) return ret;
    
    page_directory_t* dir = get_current_user_dir();
    if (!dir) return -EFAULT;
    
    size_t len = 0;
    
    if (smap_enabled) stac();
    
    while (len < max_len) {
        uint64_t page_addr = (str_addr + len) & ~PAGE_MASK;
        size_t page_offset = (str_addr + len) & PAGE_MASK;
        
        if (str_addr + len >= USER_SPACE_END) {
            if (smap_enabled) clac();
            return -EFAULT;
        }
        
        if (!check_page_permissions(dir, page_addr, false)) {
            if (smap_enabled) clac();
            return -EFAULT;
        }
        
        uint64_t phys = vmm_get_physical(dir, page_addr);
        if (!phys) {
            if (smap_enabled) clac();
            return -EFAULT;
        }
        
        volatile uint8_t* page_ptr = (volatile uint8_t*)(phys + page_offset);
        
        while (page_offset < PAGE_SIZE && len < max_len) {
            char c = (char)page_ptr[0];
            if (c == '\0') {
                if (smap_enabled) clac();
                return len;
            }
            len++;
            page_offset++;
            page_ptr++;
        }
    }
    
    if (smap_enabled) clac();
    
    return max_len;
}

int strncpy_from_user(char* kernel_dst, const char* user_src, size_t max_len) {
    if (!kernel_dst || !user_src || max_len == 0) return -EINVAL;
    
    ssize_t len = strnlen_user(user_src, max_len - 1);
    if (len < 0) return len;
    
    int ret = copy_from_user(kernel_dst, user_src, len);
    if (ret) return ret;
    
    kernel_dst[len] = '\0';
    return len;
}

int verify_user_read(const void* user_ptr, size_t size) {
    if (!user_ptr || size == 0) return -EINVAL;
    
    uint64_t addr = (uint64_t)user_ptr;
    int ret = validate_user_range(addr, size);
    if (ret) return ret;
    
    page_directory_t* dir = get_current_user_dir();
    if (!dir) return -EFAULT;
    
    uint64_t start_page = addr & ~PAGE_MASK;
    uint64_t end_page = ((addr + size - 1) & ~PAGE_MASK);
    
    for (uint64_t page = start_page; page <= end_page; page += PAGE_SIZE) {
        if (!check_page_permissions(dir, page, false)) {
            return -EFAULT;
        }
    }
    
    return 0;
}

int verify_user_write(void* user_ptr, size_t size) {
    if (!user_ptr || size == 0) return -EINVAL;
    
    uint64_t addr = (uint64_t)user_ptr;
    int ret = validate_user_range(addr, size);
    if (ret) return ret;
    
    page_directory_t* dir = get_current_user_dir();
    if (!dir) return -EFAULT;
    
    uint64_t start_page = addr & ~PAGE_MASK;
    uint64_t end_page = ((addr + size - 1) & ~PAGE_MASK);
    
    for (uint64_t page = start_page; page <= end_page; page += PAGE_SIZE) {
        if (!check_page_permissions(dir, page, true)) {
            return -EFAULT;
        }
    }
    
    return 0;
}

int clear_user(void* user_dst, size_t size) {
    if (!user_dst || size == 0) return -EINVAL;
    
    uint64_t dst_addr = (uint64_t)user_dst;
    int ret = validate_user_range(dst_addr, size);
    if (ret) return ret;
    
    page_directory_t* dir = get_current_user_dir();
    if (!dir) return -EFAULT;
    
    size_t cleared = 0;
    
    if (smap_enabled) stac();
    
    while (cleared < size) {
        uint64_t page_addr = (dst_addr + cleared) & ~PAGE_MASK;
        size_t page_offset = (dst_addr + cleared) & PAGE_MASK;
        size_t chunk_size = PAGE_SIZE - page_offset;
        if (chunk_size > size - cleared) {
            chunk_size = size - cleared;
        }
        
        if (!check_page_permissions(dir, page_addr, true)) {
            if (smap_enabled) clac();
            return -EFAULT;
        }
        
        uint64_t phys = vmm_get_physical(dir, page_addr);
        if (!phys) {
            if (smap_enabled) clac();
            return -EFAULT;
        }
        
        volatile uint8_t* dst_page = (volatile uint8_t*)(phys + page_offset);
        
        if (chunk_size >= FAST_COPY_THRESHOLD && ((uint64_t)dst_page & 7) == 0) {
            size_t qwords = chunk_size / 8;
            uint64_t* dst_q = (uint64_t*)dst_page;
            for (size_t i = 0; i < qwords; i++) {
                dst_q[i] = 0;
            }
            size_t fast_bytes = qwords * 8;
            cleared += fast_bytes;
            chunk_size -= fast_bytes;
            dst_page += fast_bytes;
        }
        
        for (size_t i = 0; i < chunk_size; i++) {
            dst_page[i] = 0;
        }
        
        cleared += chunk_size;
    }
    
    if (smap_enabled) clac();
    
    return 0;
}

int get_user_u8(uint8_t* kernel_dst, const uint8_t* user_src) {
    return copy_from_user(kernel_dst, user_src, sizeof(uint8_t));
}

int get_user_u16(uint16_t* kernel_dst, const uint16_t* user_src) {
    return copy_from_user(kernel_dst, user_src, sizeof(uint16_t));
}

int get_user_u32(uint32_t* kernel_dst, const uint32_t* user_src) {
    return copy_from_user(kernel_dst, user_src, sizeof(uint32_t));
}

int get_user_u64(uint64_t* kernel_dst, const uint64_t* user_src) {
    return copy_from_user(kernel_dst, user_src, sizeof(uint64_t));
}

int put_user_u8(uint8_t* user_dst, uint8_t value) {
    return copy_to_user(user_dst, &value, sizeof(uint8_t));
}

int put_user_u16(uint16_t* user_dst, uint16_t value) {
    return copy_to_user(user_dst, &value, sizeof(uint16_t));
}

int put_user_u32(uint32_t* user_dst, uint32_t value) {
    return copy_to_user(user_dst, &value, sizeof(uint32_t));
}

int put_user_u64(uint64_t* user_dst, uint64_t value) {
    return copy_to_user(user_dst, &value, sizeof(uint64_t));
}