#include <firmware/acpi.h>

#include <drivers/serial.h>
#include <kernel/multiboot2.h>
#include <kernel/mm/vmm.h>
#include <kernel/string.h>

#define ACPI_RSDP_SEARCH_START 0x000E0000ULL
#define ACPI_RSDP_SEARCH_END   0x00100000ULL
#define ACPI_RSDP_SCAN_STEP    16u

typedef struct {
    char signature[8];
    uint8_t checksum;
    char oem_id[6];
    uint8_t revision;
    uint32_t rsdt_address;
    uint32_t length;
    uint64_t xsdt_address;
    uint8_t extended_checksum;
    uint8_t reserved[3];
} __attribute__((packed)) acpi_rsdp_t;

typedef struct {
    acpi_sdt_header_t header;
    uint64_t reserved;
} __attribute__((packed)) acpi_mcfg_table_t;

typedef struct {
    uint64_t base_address;
    uint16_t segment_group;
    uint8_t bus_start;
    uint8_t bus_end;
    uint32_t reserved;
} __attribute__((packed)) acpi_mcfg_alloc_t;

typedef struct {
    acpi_sdt_header_t header;
    uint32_t lapic_address;
    uint32_t flags;
} __attribute__((packed)) acpi_madt_t;

typedef struct {
    uint8_t type;
    uint8_t length;
} __attribute__((packed)) acpi_madt_entry_t;

typedef struct {
    acpi_madt_entry_t hdr;
    uint8_t processor_uid;
    uint8_t apic_id;
    uint32_t flags;
} __attribute__((packed)) acpi_madt_lapic_t;

typedef struct {
    acpi_madt_entry_t hdr;
    uint8_t ioapic_id;
    uint8_t reserved;
    uint32_t ioapic_address;
    uint32_t gsi_base;
} __attribute__((packed)) acpi_madt_ioapic_t;

typedef struct {
    bool initialized;
    bool available;
    bool use_xsdt;
    uint8_t rsdp_revision;
    uint64_t root_phys;
} acpi_state_t;

static acpi_state_t g_acpi;

static bool acpi_phys_range_ok(uint64_t phys, size_t size) {
    if (size == 0u) return false;
    uint64_t end = phys + (uint64_t)size - 1u;
    if (end < phys) return false;
    /* Current early mapping covers the first 4 GiB. */
    return end < 0x100000000ULL;
}

static const uint8_t* acpi_phys_ptr(uint64_t phys, size_t size) {
    if (!acpi_phys_range_ok(phys, size)) return NULL;
    return (const uint8_t*)PHYS_TO_VIRT(phys);
}

static uint8_t acpi_checksum(const void* data, size_t size) {
    if (!data) return 1u;
    const uint8_t* p = (const uint8_t*)data;
    uint8_t sum = 0u;
    for (size_t i = 0; i < size; i++) {
        sum = (uint8_t)(sum + p[i]);
    }
    return sum;
}

static bool acpi_validate_rsdp_blob(const uint8_t* blob, size_t size, acpi_rsdp_t* out_rsdp) {
    if (!blob || size < 20u || !out_rsdp) return false;
    if (memcmp(blob, "RSD PTR ", 8) != 0) return false;
    if (acpi_checksum(blob, 20u) != 0u) return false;

    memset(out_rsdp, 0, sizeof(*out_rsdp));
    memcpy(out_rsdp, blob, 20u);

    if (out_rsdp->revision >= 2u) {
        if (size < sizeof(acpi_rsdp_t)) return false;
        memcpy(out_rsdp, blob, sizeof(acpi_rsdp_t));
        if (out_rsdp->length < sizeof(acpi_rsdp_t)) return false;
        if (out_rsdp->length > size) return false;
        if (acpi_checksum(blob, out_rsdp->length) != 0u) return false;
    }
    return true;
}

static bool acpi_read_table_header(uint64_t phys, acpi_sdt_header_t* out) {
    if (!out) return false;
    const acpi_sdt_header_t* hdr = (const acpi_sdt_header_t*)acpi_phys_ptr(phys, sizeof(acpi_sdt_header_t));
    if (!hdr) return false;
    *out = *hdr;
    if (out->length < sizeof(acpi_sdt_header_t)) return false;
    const uint8_t* table = acpi_phys_ptr(phys, out->length);
    if (!table) return false;
    return acpi_checksum(table, out->length) == 0u;
}

static bool acpi_find_rsdp_in_range(uint64_t start, uint64_t end, acpi_rsdp_t* out_rsdp) {
    if (!out_rsdp) return false;
    if (end <= start || start % ACPI_RSDP_SCAN_STEP != 0u) return false;

    for (uint64_t phys = start; phys + 20u <= end; phys += ACPI_RSDP_SCAN_STEP) {
        const uint8_t* p = acpi_phys_ptr(phys, sizeof(acpi_rsdp_t));
        if (!p) continue;
        if (memcmp(p, "RSD PTR ", 8) != 0) continue;

        if (acpi_validate_rsdp_blob(p, sizeof(acpi_rsdp_t), out_rsdp)) {
            return true;
        }
    }
    return false;
}

static bool acpi_find_rsdp_via_bios_scan(acpi_rsdp_t* out_rsdp) {
    if (!out_rsdp) return false;

    const uint16_t* ebda_ptr = (const uint16_t*)acpi_phys_ptr(0x40Eu, sizeof(uint16_t));
    if (ebda_ptr) {
        uint64_t ebda_phys = (uint64_t)(*ebda_ptr) << 4;
        if (ebda_phys >= 0x80000u && ebda_phys < ACPI_RSDP_SEARCH_END) {
            uint64_t ebda_end = ebda_phys + 1024u;
            if (ebda_end > ACPI_RSDP_SEARCH_END) ebda_end = ACPI_RSDP_SEARCH_END;
            if (acpi_find_rsdp_in_range(ebda_phys & ~(uint64_t)(ACPI_RSDP_SCAN_STEP - 1u), ebda_end, out_rsdp)) {
                return true;
            }
        }
    }

    return acpi_find_rsdp_in_range(ACPI_RSDP_SEARCH_START, ACPI_RSDP_SEARCH_END, out_rsdp);
}

static bool acpi_locate_root_table(const acpi_rsdp_t* rsdp, bool* out_use_xsdt, uint64_t* out_phys) {
    if (!rsdp || !out_use_xsdt || !out_phys) return false;

    *out_use_xsdt = false;
    *out_phys = 0u;

    if (rsdp->revision >= 2u && rsdp->xsdt_address != 0u) {
        acpi_sdt_header_t xsdt_hdr;
        if (acpi_read_table_header(rsdp->xsdt_address, &xsdt_hdr) &&
            memcmp(xsdt_hdr.signature, "XSDT", 4) == 0) {
            *out_use_xsdt = true;
            *out_phys = rsdp->xsdt_address;
            return true;
        }
    }

    if (rsdp->rsdt_address != 0u) {
        uint64_t rsdt_phys = (uint64_t)rsdp->rsdt_address;
        acpi_sdt_header_t rsdt_hdr;
        if (acpi_read_table_header(rsdt_phys, &rsdt_hdr) &&
            memcmp(rsdt_hdr.signature, "RSDT", 4) == 0) {
            *out_use_xsdt = false;
            *out_phys = rsdt_phys;
            return true;
        }
    }

    return false;
}

bool acpi_init(void) {
    if (g_acpi.initialized) {
        return g_acpi.available;
    }

    memset(&g_acpi, 0, sizeof(g_acpi));
    g_acpi.initialized = true;

    acpi_rsdp_t rsdp;
    memset(&rsdp, 0, sizeof(rsdp));
    bool found = false;
    const char* source = "bios-scan";

    size_t rsdp_size = 0;
    const uint8_t* rsdp_blob = (const uint8_t*)multiboot_get_acpi_rsdp_new(&rsdp_size);
    if (rsdp_blob && acpi_validate_rsdp_blob(rsdp_blob, rsdp_size, &rsdp)) {
        found = true;
        source = "multiboot-new";
    }

    if (!found) {
        rsdp_blob = (const uint8_t*)multiboot_get_acpi_rsdp_old(&rsdp_size);
        if (rsdp_blob && acpi_validate_rsdp_blob(rsdp_blob, rsdp_size, &rsdp)) {
            found = true;
            source = "multiboot-old";
        }
    }

    if (!found) {
        found = acpi_find_rsdp_via_bios_scan(&rsdp);
    }

    if (!found) {
        serial_write("[ACPI] RSDP not found\n");
        return false;
    }

    bool use_xsdt = false;
    uint64_t root_phys = 0u;
    if (!acpi_locate_root_table(&rsdp, &use_xsdt, &root_phys)) {
        serial_write("[ACPI] Root table (XSDT/RSDT) not available\n");
        return false;
    }

    g_acpi.available = true;
    g_acpi.use_xsdt = use_xsdt;
    g_acpi.rsdp_revision = rsdp.revision;
    g_acpi.root_phys = root_phys;

    serial_write("[ACPI] RSDP revision ");
    serial_write_dec((uint32_t)g_acpi.rsdp_revision);
    serial_write(" via ");
    serial_write(source);
    serial_write("\n");
    serial_write("[ACPI] Root table: ");
    serial_write(use_xsdt ? "XSDT @" : "RSDT @");
    serial_write_hex(root_phys);
    serial_write("\n");
    return true;
}

bool acpi_available(void) {
    if (!g_acpi.initialized) {
        (void)acpi_init();
    }
    return g_acpi.available;
}

const acpi_sdt_header_t* acpi_find_table(const char signature[4]) {
    if (!signature) return NULL;
    if (!acpi_available()) return NULL;

    acpi_sdt_header_t root_hdr;
    if (!acpi_read_table_header(g_acpi.root_phys, &root_hdr)) return NULL;

    const uint8_t* root = acpi_phys_ptr(g_acpi.root_phys, root_hdr.length);
    if (!root) return NULL;

    size_t entry_size = g_acpi.use_xsdt ? sizeof(uint64_t) : sizeof(uint32_t);
    size_t entries_offset = sizeof(acpi_sdt_header_t);
    if (root_hdr.length < entries_offset) return NULL;
    size_t bytes = (size_t)root_hdr.length - entries_offset;
    size_t count = bytes / entry_size;

    for (size_t i = 0; i < count; i++) {
        uint64_t table_phys = 0u;
        if (g_acpi.use_xsdt) {
            const uint64_t* p = (const uint64_t*)(root + entries_offset + (i * entry_size));
            table_phys = *p;
        } else {
            const uint32_t* p = (const uint32_t*)(root + entries_offset + (i * entry_size));
            table_phys = (uint64_t)(*p);
        }
        if (table_phys == 0u) continue;

        acpi_sdt_header_t hdr;
        if (!acpi_read_table_header(table_phys, &hdr)) continue;
        if (memcmp(hdr.signature, signature, 4) != 0) continue;

        return (const acpi_sdt_header_t*)acpi_phys_ptr(table_phys, hdr.length);
    }

    return NULL;
}

bool acpi_get_mcfg_info(acpi_mcfg_info_t* out_info) {
    if (!out_info) return false;
    memset(out_info, 0, sizeof(*out_info));

    const acpi_mcfg_table_t* mcfg = (const acpi_mcfg_table_t*)acpi_find_table("MCFG");
    if (!mcfg) return false;

    if (mcfg->header.length < sizeof(acpi_mcfg_table_t)) return false;
    size_t bytes = (size_t)mcfg->header.length - sizeof(acpi_mcfg_table_t);
    size_t count = bytes / sizeof(acpi_mcfg_alloc_t);
    const acpi_mcfg_alloc_t* seg = (const acpi_mcfg_alloc_t*)((const uint8_t*)mcfg + sizeof(acpi_mcfg_table_t));

    const acpi_mcfg_alloc_t* best = NULL;
    for (size_t i = 0; i < count; i++) {
        if (seg[i].bus_end < seg[i].bus_start) continue;
        if (!best || seg[i].segment_group == 0u) {
            best = &seg[i];
            if (best->segment_group == 0u) break;
        }
    }
    if (!best) return false;

    out_info->present = true;
    out_info->base_address = best->base_address;
    out_info->segment_group = best->segment_group;
    out_info->bus_start = best->bus_start;
    out_info->bus_end = best->bus_end;
    return true;
}

bool acpi_get_madt_info(acpi_madt_info_t* out_info) {
    if (!out_info) return false;
    memset(out_info, 0, sizeof(*out_info));

    const acpi_madt_t* madt = (const acpi_madt_t*)acpi_find_table("APIC");
    if (!madt) return false;

    if (madt->header.length < sizeof(acpi_madt_t)) return false;

    out_info->present = true;
    out_info->lapic_address = madt->lapic_address;
    out_info->flags = madt->flags;

    const uint8_t* cur = (const uint8_t*)madt + sizeof(acpi_madt_t);
    const uint8_t* end = (const uint8_t*)madt + madt->header.length;

    while (cur + sizeof(acpi_madt_entry_t) <= end) {
        const acpi_madt_entry_t* entry = (const acpi_madt_entry_t*)cur;
        if (entry->length < sizeof(acpi_madt_entry_t)) break;
        if (cur + entry->length > end) break;

        if (entry->type == 0u && entry->length >= sizeof(acpi_madt_lapic_t)) {
            const acpi_madt_lapic_t* lapic = (const acpi_madt_lapic_t*)entry;
            if ((lapic->flags & 0x1u) != 0u) {
                out_info->cpu_count++;
            }
        } else if (entry->type == 1u && entry->length >= sizeof(acpi_madt_ioapic_t)) {
            const acpi_madt_ioapic_t* ioapic = (const acpi_madt_ioapic_t*)entry;
            if (!out_info->has_ioapic) {
                out_info->has_ioapic = true;
                out_info->ioapic_id = ioapic->ioapic_id;
                out_info->ioapic_address = ioapic->ioapic_address;
                out_info->ioapic_gsi_base = ioapic->gsi_base;
            }
        }

        cur += entry->length;
    }

    return true;
}
