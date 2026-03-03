#include <drivers/apic.h>

#include <arch/msr.h>
#include <drivers/serial.h>
#include <firmware/acpi.h>
#include <kernel/mm/vmm.h>

#define IA32_APIC_BASE_MSR 0x1Bu
#define IA32_APIC_BASE_BSP_BIT (1ULL << 8)
#define IA32_APIC_BASE_ENABLE_BIT (1ULL << 11)
#define IA32_APIC_BASE_ADDR_MASK 0x000FFFFFFFFFF000ULL

#define LAPIC_REG_ID   0x20u
#define LAPIC_REG_EOI  0xB0u
#define LAPIC_REG_SVR  0xF0u
#define LAPIC_SVR_ENABLE_BIT 0x00000100u
#define LAPIC_SPURIOUS_VECTOR 0xFFu

typedef struct {
    bool initialized;
    bool present;
    bool enabled;
    uint64_t lapic_base;
    volatile uint32_t* lapic_mmio;
    uint32_t lapic_id;
    bool ioapic_present;
    uint32_t ioapic_base;
    uint32_t ioapic_gsi_base;
} apic_state_t;

static apic_state_t g_apic;

static bool apic_cpu_feature_present(void) {
    uint32_t eax = 1u;
    uint32_t ebx = 0u;
    uint32_t ecx = 0u;
    uint32_t edx = 0u;
    __asm__ volatile("cpuid"
                     : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                     :
                     : "memory");
    (void)ebx;
    (void)ecx;
    return (edx & (1u << 9)) != 0u;
}

static inline bool apic_phys_mapped(uint64_t phys) {
    return phys < 0x100000000ULL;
}

static inline volatile uint32_t* apic_mmio_ptr(uint64_t phys_base) {
    if (!apic_phys_mapped(phys_base + 0x1000u - 1u)) return NULL;
    return (volatile uint32_t*)PHYS_TO_VIRT(phys_base);
}

static inline uint32_t lapic_read(uint32_t reg) {
    return g_apic.lapic_mmio[reg >> 2];
}

static inline void lapic_write(uint32_t reg, uint32_t value) {
    g_apic.lapic_mmio[reg >> 2] = value;
    (void)g_apic.lapic_mmio[reg >> 2];
}

bool apic_init(void) {
    if (g_apic.initialized) {
        return g_apic.enabled;
    }
    g_apic.initialized = true;

    if (!apic_cpu_feature_present()) {
        serial_write("[APIC] CPU does not report APIC feature\n");
        return false;
    }

    uint64_t apic_base_msr = rdmsr(IA32_APIC_BASE_MSR);
    uint64_t lapic_phys = apic_base_msr & IA32_APIC_BASE_ADDR_MASK;

    acpi_madt_info_t madt;
    if (acpi_get_madt_info(&madt) && madt.present) {
        if (madt.lapic_address != 0u) {
            lapic_phys = (uint64_t)madt.lapic_address;
        }
        g_apic.ioapic_present = madt.has_ioapic;
        g_apic.ioapic_base = madt.ioapic_address;
        g_apic.ioapic_gsi_base = madt.ioapic_gsi_base;
    }

    if (lapic_phys == 0u || !apic_phys_mapped(lapic_phys)) {
        serial_write("[APIC] Local APIC not available in mapped physical range\n");
        return false;
    }

    g_apic.lapic_base = lapic_phys;
    g_apic.lapic_mmio = apic_mmio_ptr(lapic_phys);
    if (!g_apic.lapic_mmio) {
        serial_write("[APIC] Failed to map local APIC MMIO\n");
        return false;
    }

    g_apic.present = true;

    if ((apic_base_msr & IA32_APIC_BASE_ENABLE_BIT) == 0u) {
        apic_base_msr |= IA32_APIC_BASE_ENABLE_BIT;
        wrmsr(IA32_APIC_BASE_MSR, apic_base_msr);
    }

    uint32_t svr = lapic_read(LAPIC_REG_SVR);
    svr |= LAPIC_SVR_ENABLE_BIT;
    svr = (svr & ~0xFFu) | LAPIC_SPURIOUS_VECTOR;
    lapic_write(LAPIC_REG_SVR, svr);

    g_apic.lapic_id = lapic_read(LAPIC_REG_ID) >> 24;
    g_apic.enabled = true;

    serial_write("[APIC] Local APIC enabled at ");
    serial_write_hex(g_apic.lapic_base);
    serial_write(" id=");
    serial_write_dec(g_apic.lapic_id);
    serial_write(" mode=");
    serial_write((apic_base_msr & IA32_APIC_BASE_BSP_BIT) ? "bsp" : "ap");
    serial_write("\n");

    if (g_apic.ioapic_present) {
        serial_write("[APIC] IOAPIC present at ");
        serial_write_hex(g_apic.ioapic_base);
        serial_write(" gsi_base=");
        serial_write_dec(g_apic.ioapic_gsi_base);
        serial_write("\n");
    } else {
        serial_write("[APIC] IOAPIC not reported by MADT\n");
    }

    return true;
}

bool apic_available(void) {
    return g_apic.present;
}

bool apic_enabled(void) {
    return g_apic.enabled;
}

void apic_send_eoi(void) {
    if (!g_apic.enabled || !g_apic.lapic_mmio) return;
    lapic_write(LAPIC_REG_EOI, 0u);
}

bool apic_get_info(apic_info_t* out_info) {
    if (!out_info) return false;
    out_info->present = g_apic.present;
    out_info->enabled = g_apic.enabled;
    out_info->lapic_base = g_apic.lapic_base;
    out_info->lapic_id = g_apic.lapic_id;
    out_info->ioapic_present = g_apic.ioapic_present;
    out_info->ioapic_base = g_apic.ioapic_base;
    out_info->ioapic_gsi_base = g_apic.ioapic_gsi_base;
    return g_apic.present;
}
