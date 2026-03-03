#include <mm/pagefault.h>
#include <arch/idt.h>
#include <drivers/serial.h>
#include <debug/panic.h>
#include <task/task.h>

static uint64_t pagefault_count = 0;

static uint64_t frame_rbp(uint64_t frame_ptr) {
    if (!frame_ptr) return 0u;
    return ((uint64_t*)frame_ptr)[8];
}

static uint64_t frame_rsp(uint64_t cs, uint64_t frame_ptr) {
    if (!frame_ptr) return 0u;
    if ((cs & 0x3u) == 0x3u) {
        return ((uint64_t*)frame_ptr)[20];
    }
    return frame_ptr + 160u;
}

void pagefault_handler(uint64_t error_code,
                       uint64_t faulting_addr,
                       uint64_t rip,
                       uint64_t cs,
                       uint64_t rflags,
                       uint64_t frame_ptr) {
    pagefault_count++;
    
    serial_write("\n[PAGE FAULT #");
    serial_write_dec(pagefault_count);
    serial_write("]\n  Address: ");
    serial_write_hex(faulting_addr);
    serial_write("\n  RIP: ");
    serial_write_hex(rip);
    serial_write("\n  Error code: ");
    serial_write_hex(error_code);
    serial_write("\n");
    
    if (error_code & 0x1) serial_write("  - Page present\n");
    else serial_write("  - Page not present\n");
    
    if (error_code & 0x2) serial_write("  - Write access\n");
    else serial_write("  - Read access\n");
    
    if (error_code & 0x4) serial_write("  - User mode\n");
    else serial_write("  - Kernel mode\n");
    
    if (error_code & 0x10) serial_write("  - Instruction fetch\n");
    
    task_t* current = get_current_task();
    if (current && current->page_dir) {
        uint64_t* pml4 = (uint64_t*)((uint64_t)(current->page_dir->pml4_phys) + 0xFFFF800000000000ULL);
        size_t pml4_idx = (faulting_addr >> 39) & 0x1FF;
        uint64_t pml4_entry = pml4[pml4_idx];
        
        serial_write("  PML4[");
        serial_write_dec(pml4_idx);
        serial_write("] = ");
        serial_write_hex(pml4_entry);
        serial_write("\n");
        
        if (pml4_entry & 1) {
            uint64_t* pdp = (uint64_t*)(((pml4_entry & ~0xFFFULL) + 0xFFFF800000000000ULL));
            size_t pdp_idx = (faulting_addr >> 30) & 0x1FF;
            uint64_t pdp_entry = pdp[pdp_idx];
            
            serial_write("  PDP[");
            serial_write_dec(pdp_idx);
            serial_write("] = ");
            serial_write_hex(pdp_entry);
            serial_write("\n");
            
            if (pdp_entry & 1) {
                uint64_t* pd = (uint64_t*)(((pdp_entry & ~0xFFFULL) + 0xFFFF800000000000ULL));
                size_t pd_idx = (faulting_addr >> 21) & 0x1FF;
                uint64_t pd_entry = pd[pd_idx];
                
                serial_write("  PD[");
                serial_write_dec(pd_idx);
                serial_write("] = ");
                serial_write_hex(pd_entry);
                serial_write("\n");
                
                if (pd_entry & 1) {
                    if (pd_entry & (1ULL << 7)) {
                        serial_write("  [2MB page mapping at PD level]\n");
                    } else {
                    uint64_t* pt = (uint64_t*)(((pd_entry & ~0xFFFULL) + 0xFFFF800000000000ULL));
                    size_t pt_idx = (faulting_addr >> 12) & 0x1FF;
                    uint64_t pt_entry = pt[pt_idx];
                    
                    serial_write("  PT[");
                    serial_write_dec(pt_idx);
                    serial_write("] = ");
                    serial_write_hex(pt_entry);
                    
                    if (pt_entry & (1ULL << 63)) {
                        serial_write(" [NX BIT SET - NOT EXECUTABLE!]");
                    }
                    serial_write("\n");
                    }
                }
            }
        }
    }

    panic_context_t ctx;
    ctx.vector = 14u;
    ctx.error_code = error_code;
    ctx.rip = rip;
    ctx.cs = cs;
    ctx.rflags = rflags;
    ctx.rsp = frame_rsp(cs, frame_ptr);
    ctx.rbp = frame_rbp(frame_ptr);
    ctx.cr2 = faulting_addr;

    panic_dump_and_halt("Page Fault", &ctx);
}

bool pagefault_init(void) {
    serial_write("[PF] Page Fault Handler initialized\n");
    return true;
}
