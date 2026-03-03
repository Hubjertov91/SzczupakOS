#include <debug/panic.h>
#include <arch/api.h>
#include <drivers/serial.h>
#include <mm/vmm.h>

#define KERNEL_VIRT_BASE 0xFFFF800000000000ULL
#define PANIC_BT_MAX_FRAMES 24u
#define PANIC_VECTOR_NONE ((uint64_t)-1)

static volatile uint32_t g_panic_active = 0;

static const char* k_exception_names[32] = {
    "Division By Zero",
    "Debug",
    "Non-Maskable Interrupt",
    "Breakpoint",
    "Overflow",
    "Bound Range Exceeded",
    "Invalid Opcode",
    "Device Not Available",
    "Double Fault",
    "Coprocessor Segment Overrun",
    "Invalid TSS",
    "Segment Not Present",
    "Stack-Segment Fault",
    "General Protection Fault",
    "Page Fault",
    "Reserved",
    "x87 Floating-Point Exception",
    "Alignment Check",
    "Machine Check",
    "SIMD Floating-Point Exception",
    "Virtualization Exception",
    "Control Protection Exception",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Hypervisor Injection Exception",
    "VMM Communication Exception",
    "Security Exception",
    "Reserved"
};

static inline bool panic_is_kernel_pointer(uint64_t addr) {
    return addr >= KERNEL_VIRT_BASE;
}

static bool panic_can_read_qword(uint64_t addr) {
    if ((addr & 0x7u) != 0u) return false;
    if (!panic_is_kernel_pointer(addr)) return false;

    page_directory_t* kernel_dir = vmm_get_kernel_directory();
    if (!kernel_dir) return false;

    uint64_t phys_lo = vmm_get_physical(kernel_dir, addr);
    uint64_t phys_hi = vmm_get_physical(kernel_dir, addr + 7u);
    return phys_lo != 0u && phys_hi != 0u;
}

static void panic_write_vector(const panic_context_t* ctx) {
    if (!ctx) return;
    if (ctx->vector == PANIC_VECTOR_NONE) return;

    serial_write("[PANIC] Vector: ");
    serial_write_dec((uint32_t)ctx->vector);
    serial_write(" (");
    serial_write(panic_exception_name(ctx->vector));
    serial_write(")\n");
}

static void panic_show_vga_banner(void) {
    volatile uint16_t* vga = (volatile uint16_t*)0xB8000;
    const char* msg = "!!! KERNEL PANIC - CHECK SERIAL LOG !!!";
    for (int i = 0; msg[i] != '\0'; i++) {
        vga[160 + i] = 0x4F00 | (uint16_t)msg[i];
    }
}

const char* panic_exception_name(uint64_t vector) {
    if (vector < 32u) {
        return k_exception_names[vector];
    }
    return "Unknown Exception";
}

void panic_print_backtrace(uint64_t rbp, uint64_t rip) {
    serial_write("[PANIC] Backtrace:\n");

    if (rip != 0u) {
        serial_write("  #00 ");
        serial_write_hex(rip);
        serial_write("\n");
    }

    if (!panic_is_kernel_pointer(rbp) || (rbp & 0x7u) != 0u) {
        serial_write("  (no valid kernel frame pointer)\n");
        return;
    }

    uint64_t frame = rbp;
    for (uint32_t depth = 1u; depth < PANIC_BT_MAX_FRAMES; depth++) {
        if (!panic_can_read_qword(frame) || !panic_can_read_qword(frame + 8u)) {
            serial_write("  (unwinder stopped: unmapped frame)\n");
            return;
        }

        uint64_t next = *(uint64_t*)frame;
        uint64_t ret = *((uint64_t*)frame + 1u);

        serial_write("  #");
        if (depth < 10u) serial_write_char('0');
        serial_write_dec(depth);
        serial_write(" ");
        serial_write_hex(ret);
        serial_write("\n");

        if (next <= frame || (next - frame) > 0x100000u || (next & 0x7u) != 0u) {
            serial_write("  (unwinder stopped: invalid frame chain)\n");
            return;
        }
        if (!panic_is_kernel_pointer(next)) {
            serial_write("  (unwinder stopped: non-kernel frame)\n");
            return;
        }
        frame = next;
    }
}

void panic_dump_and_halt(const char* reason, const panic_context_t* ctx) {
    arch_irq_disable();

    if (__atomic_exchange_n(&g_panic_active, 1u, __ATOMIC_ACQ_REL) != 0u) {
        serial_write("\n[PANIC] Recursive panic detected, halting.\n");
        arch_halt_forever();
    }

    serial_write("\n========================================\n");
    serial_write("KERNEL PANIC\n");
    serial_write("========================================\n");
    serial_write("[PANIC] Reason: ");
    serial_write((reason && reason[0] != '\0') ? reason : "(unspecified)");
    serial_write("\n");

    if (ctx) {
        panic_write_vector(ctx);
        serial_write("[PANIC] Error Code: ");
        serial_write_hex(ctx->error_code);
        serial_write("\n");
        serial_write("[PANIC] RIP: ");
        serial_write_hex(ctx->rip);
        serial_write("\n");
        serial_write("[PANIC] CS: ");
        serial_write_hex(ctx->cs);
        serial_write("\n");
        serial_write("[PANIC] RFLAGS: ");
        serial_write_hex(ctx->rflags);
        serial_write("\n");
        serial_write("[PANIC] RSP: ");
        serial_write_hex(ctx->rsp);
        serial_write("\n");
        serial_write("[PANIC] RBP: ");
        serial_write_hex(ctx->rbp);
        serial_write("\n");
        if (ctx->cr2 != 0u) {
            serial_write("[PANIC] CR2: ");
            serial_write_hex(ctx->cr2);
            serial_write("\n");
        }
        panic_print_backtrace(ctx->rbp, ctx->rip);
    } else {
        serial_write("[PANIC] (no CPU context available)\n");
    }

    serial_write("[PANIC] Recent serial log tail:\n");
    (void)serial_log_dump_tail(2048u);
    serial_write("\n");
    serial_write("========================================\n");
    serial_write("[PANIC] CPU halted.\n");

    panic_show_vga_banner();
    arch_halt_forever();
}

void panic_halt_message(const char* reason) {
    panic_context_t ctx;
    ctx.vector = PANIC_VECTOR_NONE;
    ctx.error_code = 0u;
    ctx.cr2 = 0u;
    ctx.cs = 0u;
    __asm__ volatile("lea (%%rip), %0" : "=r"(ctx.rip));
    __asm__ volatile("mov %%rsp, %0" : "=r"(ctx.rsp));
    __asm__ volatile("mov %%rbp, %0" : "=r"(ctx.rbp));
    __asm__ volatile("pushfq; pop %0" : "=r"(ctx.rflags));

    panic_dump_and_halt(reason, &ctx);
}
