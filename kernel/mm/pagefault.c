#include <kernel/serial.h>
#include <kernel/vga.h>
#include <kernel/pmm.h>
#include <kernel/vmm.h>
#include <kernel/pagefault.h>

static uint64_t pagefault_count = 0;

void pagefault_init(void) {
    serial_write("[PF] Page Fault Handler initialzzized\n");
}

void pagefault_handler(uint64_t error_code, uint64_t faulting_addr) {
    pagefault_count++;
    
    serial_write("[PF] #");
    serial_write_dec(pagefault_count);
    serial_write(" at 0x");
    serial_write_hex(faulting_addr);
    serial_write(" err=0x");
    serial_write_hex(error_code);
    serial_write("\n");
    
    bool present = error_code & PF_PRESENT;
    bool write = error_code & PF_WRITE;
    bool user = error_code & PF_USER;
    bool reserved = error_code & PF_RESERVED;
    
    serial_write("[PF]");
    if (!present) serial_write(" NOT_PRESENT");
    if (write) serial_write(" WRITE");
    if (user) serial_write(" USER");
    if (reserved) serial_write(" RESERVED");
    serial_write("\n");
    
    if (!present && !reserved && faulting_addr >= 0x10000000) {
        page_directory_t* dir = vmm_get_kernel_directory();
        uint64_t page_addr = faulting_addr & ~0xFFFULL;
        
        uint64_t phys = pmm_alloc_page();
        if (phys) {
            uint32_t flags = PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
            
            if (vmm_map_page(dir, page_addr, phys, flags)) {
                volatile char* p = (volatile char*)page_addr;
                for (int i = 0; i < 4096; i++) p[i] = 0;
                
                serial_write("[PF] Demand paging OK\n");
                return;
            }
            pmm_free_page(phys);
        }
    }
    
    vga_set_color(VGA_COLOR_RED, VGA_COLOR_BLACK);
    vga_write("\n!!! PAGE FAULT !!!\nAddr: 0x");
    char buf[17];
    for (int i = 0; i < 16; i++) {
        uint8_t n = (faulting_addr >> (60 - i * 4)) & 0xF;
        buf[i] = (n < 10) ? ('0' + n) : ('A' + n - 10);
    }
    buf[16] = '\0';
    vga_write(buf);
    vga_write("\n");
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    
    serial_write("[PF] FATAL\n");
    __asm__ volatile("cli; hlt");
    while(1) __asm__ volatile("hlt");
}