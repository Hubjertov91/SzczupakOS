#ifndef _KERNEL_DRIVERS_APIC_H
#define _KERNEL_DRIVERS_APIC_H

#include <kernel/stdint.h>

typedef struct {
    bool present;
    bool enabled;
    uint64_t lapic_base;
    uint32_t lapic_id;
    bool ioapic_present;
    uint32_t ioapic_base;
    uint32_t ioapic_gsi_base;
} apic_info_t;

bool apic_init(void);
bool apic_available(void);
bool apic_enabled(void);
void apic_send_eoi(void);
bool apic_get_info(apic_info_t* out_info);

#endif
