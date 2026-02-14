#include <kernel/gdt.h>
#include <kernel/tss.h>
#include <kernel/serial.h>

typedef struct {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_mid;
    uint8_t access;
    uint8_t granularity;
    uint8_t base_high;
} __attribute__((packed)) gdt_entry_t;

typedef struct {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed)) gdt_ptr_t;

#define GDT_ENTRIES 7

static gdt_entry_t gdt[GDT_ENTRIES];
static gdt_ptr_t gdt_ptr;

extern void gdt_flush(uint64_t);
extern void tss_flush(void);

static void gdt_set_gate(int num, uint64_t base, uint64_t limit, uint8_t access, uint8_t gran) {
    gdt[num].base_low = base & 0xFFFF;
    gdt[num].base_mid = (base >> 16) & 0xFF;
    gdt[num].base_high = (base >> 24) & 0xFF;
    gdt[num].limit_low = limit & 0xFFFF;
    gdt[num].granularity = (limit >> 16) & 0x0F;
    gdt[num].granularity |= gran & 0xF0;
    gdt[num].access = access;
    
    if (num >= 5) { 
        uint64_t* ext = (uint64_t*)&gdt[num + 1];
        *ext = base >> 32;
    }
}
void gdt_init(void) {
    gdt_ptr.limit = (sizeof(gdt_entry_t) * GDT_ENTRIES) - 1;
    gdt_ptr.base = (uint64_t)&gdt;
    
    serial_write("[GDT] Initializing Global Descriptor Table\n");
    
    gdt_set_gate(0, 0, 0, 0, 0);
    gdt_set_gate(1, 0, 0, 0x9A, 0xAF);
    gdt_set_gate(2, 0, 0, 0x92, 0xCF);
    gdt_set_gate(3, 0, 0, 0xFA, 0xAF);
    gdt_set_gate(4, 0, 0, 0xF2, 0xCF);
    
    serial_write("[GDT] Segments configured:\n");
    serial_write("  Kernel CS = 0x08, Kernel DS = 0x10\n");
    serial_write("  User CS = 0x1B (0x18|3), User DS = 0x23 (0x20|3)\n");
    
    tss_init();
    uint64_t tss_base = (uint64_t)tss_get_address();
    gdt_set_gate(5, tss_base & 0xFFFFFFFF, sizeof(tss_t) - 1, 0x89, 0x00);
    uint64_t* tss_high = (uint64_t*)&gdt[6];
    *tss_high = tss_base >> 32;
    
    serial_write("[GDT] TSS at 0x");
    serial_write_hex(tss_base);
    serial_write("\n");
    
    gdt_flush((uint64_t)&gdt_ptr);
    tss_flush();
    
    serial_write("[GDT] User mode ready\n");
}

void gdt_set_kernel_stack(uint64_t stack) {
    tss_set_kernel_stack(stack);
}