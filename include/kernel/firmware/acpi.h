#ifndef _KERNEL_FIRMWARE_ACPI_H
#define _KERNEL_FIRMWARE_ACPI_H

#include <kernel/stdint.h>

typedef struct {
    char signature[4];
    uint32_t length;
    uint8_t revision;
    uint8_t checksum;
    char oem_id[6];
    char oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} __attribute__((packed)) acpi_sdt_header_t;

typedef struct {
    bool present;
    uint64_t base_address;
    uint16_t segment_group;
    uint8_t bus_start;
    uint8_t bus_end;
} acpi_mcfg_info_t;

typedef struct {
    bool present;
    uint32_t lapic_address;
    uint32_t flags;
    uint32_t cpu_count;
    bool has_ioapic;
    uint8_t ioapic_id;
    uint32_t ioapic_address;
    uint32_t ioapic_gsi_base;
} acpi_madt_info_t;

bool acpi_init(void);
bool acpi_available(void);
const acpi_sdt_header_t* acpi_find_table(const char signature[4]);
bool acpi_get_mcfg_info(acpi_mcfg_info_t* out_info);
bool acpi_get_madt_info(acpi_madt_info_t* out_info);

#endif
