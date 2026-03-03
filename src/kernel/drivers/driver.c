#include <drivers/driver.h>

#include <drivers/pci.h>
#include <drivers/serial.h>
#include <kernel/string.h>

#define DRIVER_MAX_DESCRIPTORS 64u
#define DRIVER_MAX_BINDINGS 128u

typedef struct {
    driver_desc_t entries[DRIVER_MAX_DESCRIPTORS];
    uint16_t count;
    driver_binding_t bindings[DRIVER_MAX_BINDINGS];
    uint16_t binding_count;
    bool initialized;
} driver_model_state_t;

static driver_model_state_t g_driver_model;

static const char* driver_bus_name(driver_bus_t bus_type) {
    switch (bus_type) {
        case DRIVER_BUS_PLATFORM: return "platform";
        case DRIVER_BUS_PCI: return "pci";
        case DRIVER_BUS_USB: return "usb";
        default: return "unknown";
    }
}

static bool match_u16(uint16_t expected, uint16_t actual) {
    return expected == DRIVER_MATCH_ANY_U16 || expected == actual;
}

static bool match_u8(uint8_t expected, uint8_t actual) {
    return expected == DRIVER_MATCH_ANY_U8 || expected == actual;
}

static bool driver_matches(const driver_desc_t* driver, const driver_probe_ctx_t* ctx) {
    if (!driver || !ctx) return false;
    if (driver->bus_type != ctx->bus_type) return false;
    if (!match_u16(driver->vendor_id, ctx->vendor_id)) return false;
    if (!match_u16(driver->device_id, ctx->device_id)) return false;
    if (!match_u8(driver->class_code, ctx->class_code)) return false;
    if (!match_u8(driver->subclass, ctx->subclass)) return false;
    if (!match_u8(driver->prog_if, ctx->prog_if)) return false;
    return true;
}

static uint8_t driver_match_specificity(const driver_desc_t* driver) {
    if (!driver) return 0;

    uint8_t score = 0;
    if (driver->vendor_id != DRIVER_MATCH_ANY_U16) score++;
    if (driver->device_id != DRIVER_MATCH_ANY_U16) score++;
    if (driver->class_code != DRIVER_MATCH_ANY_U8) score++;
    if (driver->subclass != DRIVER_MATCH_ANY_U8) score++;
    if (driver->prog_if != DRIVER_MATCH_ANY_U8) score++;
    return score;
}

static bool driver_record_binding(const driver_desc_t* driver, const driver_probe_ctx_t* ctx) {
    if (!driver || !ctx) return false;
    if (g_driver_model.binding_count >= DRIVER_MAX_BINDINGS) return false;

    driver_binding_t* out = &g_driver_model.bindings[g_driver_model.binding_count++];
    memset(out, 0, sizeof(*out));
    out->driver_name = driver->name;
    out->ctx = *ctx;
    return true;
}

static void driver_log_bound(const driver_desc_t* desc, const driver_probe_ctx_t* ctx) {
    if (!desc || !ctx) return;

    serial_write("[DRV] Bound ");
    serial_write(desc->name);
    serial_write(" -> ");
    serial_write(driver_bus_name(ctx->bus_type));

    if (ctx->bus_type == DRIVER_BUS_PCI) {
        serial_write(" ");
        serial_write_hex(((uint64_t)ctx->pci_bus << 16) |
                         ((uint64_t)ctx->pci_slot << 8) |
                         (uint64_t)ctx->pci_function);
    }

    serial_write(" VID:DID=");
    serial_write_hex(((uint64_t)ctx->vendor_id << 16) | (uint64_t)ctx->device_id);
    serial_write("\n");
}

void driver_model_init(void) {
    memset(&g_driver_model, 0, sizeof(g_driver_model));
    g_driver_model.initialized = true;
    serial_write("[DRV] Driver model initialized\n");
}

bool driver_register(const driver_desc_t* driver) {
    if (!driver || !driver->name) return false;
    if (!g_driver_model.initialized) driver_model_init();
    if (g_driver_model.count >= DRIVER_MAX_DESCRIPTORS) return false;

    g_driver_model.entries[g_driver_model.count++] = *driver;
    serial_write("[DRV] Registered descriptor: ");
    serial_write(driver->name);
    serial_write(" (bus=");
    serial_write(driver_bus_name(driver->bus_type));
    serial_write(")\n");
    return true;
}

void driver_register_builtin_descriptors(void) {
    static const driver_desc_t builtins[] = {
        {
            .name = "ata-pata",
            .bus_type = DRIVER_BUS_PCI,
            .vendor_id = DRIVER_MATCH_ANY_U16,
            .device_id = DRIVER_MATCH_ANY_U16,
            .class_code = 0x01u,
            .subclass = 0x01u,
            .prog_if = DRIVER_MATCH_ANY_U8,
            .probe = NULL
        },
        {
            .name = "rtl8168",
            .bus_type = DRIVER_BUS_PCI,
            .vendor_id = 0x10ECu,
            .device_id = 0x8168u,
            .class_code = DRIVER_MATCH_ANY_U8,
            .subclass = DRIVER_MATCH_ANY_U8,
            .prog_if = DRIVER_MATCH_ANY_U8,
            .probe = NULL
        },
        {
            .name = "gpu-display",
            .bus_type = DRIVER_BUS_PCI,
            .vendor_id = DRIVER_MATCH_ANY_U16,
            .device_id = DRIVER_MATCH_ANY_U16,
            .class_code = 0x03u,
            .subclass = DRIVER_MATCH_ANY_U8,
            .prog_if = DRIVER_MATCH_ANY_U8,
            .probe = NULL,
            .priority = 1u
        },
        {
            .name = "usb-uhci",
            .bus_type = DRIVER_BUS_PCI,
            .vendor_id = DRIVER_MATCH_ANY_U16,
            .device_id = DRIVER_MATCH_ANY_U16,
            .class_code = 0x0Cu,
            .subclass = 0x03u,
            .prog_if = 0x00u,
            .probe = NULL
        },
        {
            .name = "usb-ohci",
            .bus_type = DRIVER_BUS_PCI,
            .vendor_id = DRIVER_MATCH_ANY_U16,
            .device_id = DRIVER_MATCH_ANY_U16,
            .class_code = 0x0Cu,
            .subclass = 0x03u,
            .prog_if = 0x10u,
            .probe = NULL
        },
        {
            .name = "usb-ehci",
            .bus_type = DRIVER_BUS_PCI,
            .vendor_id = DRIVER_MATCH_ANY_U16,
            .device_id = DRIVER_MATCH_ANY_U16,
            .class_code = 0x0Cu,
            .subclass = 0x03u,
            .prog_if = 0x20u,
            .probe = NULL
        },
        {
            .name = "usb-xhci",
            .bus_type = DRIVER_BUS_PCI,
            .vendor_id = DRIVER_MATCH_ANY_U16,
            .device_id = DRIVER_MATCH_ANY_U16,
            .class_code = 0x0Cu,
            .subclass = 0x03u,
            .prog_if = 0x30u,
            .probe = NULL
        }
    };

    for (size_t i = 0; i < (sizeof(builtins) / sizeof(builtins[0])); i++) {
        (void)driver_register(&builtins[i]);
    }
}

void driver_reset_bindings(void) {
    g_driver_model.binding_count = 0;
}

bool driver_probe_context(const driver_probe_ctx_t* ctx) {
    if (!ctx) return false;
    if (!g_driver_model.initialized) driver_model_init();

    const driver_desc_t* best = NULL;
    uint16_t best_rank = 0;

    for (uint16_t d = 0; d < g_driver_model.count; d++) {
        const driver_desc_t* desc = &g_driver_model.entries[d];
        if (!driver_matches(desc, ctx)) continue;

        bool accepted = true;
        if (desc->probe) {
            accepted = desc->probe(ctx);
        }
        if (!accepted) continue;

        uint16_t rank = ((uint16_t)desc->priority << 8) | (uint16_t)driver_match_specificity(desc);
        if (!best || rank > best_rank) {
            best = desc;
            best_rank = rank;
        }
    }

    if (!best) return false;
    if (!driver_record_binding(best, ctx)) return false;
    driver_log_bound(best, ctx);
    return true;
}

void driver_probe_all(void) {
    if (!g_driver_model.initialized) driver_model_init();
    driver_reset_bindings();

    uint16_t pci_count = pci_get_device_count();
    for (uint16_t i = 0; i < pci_count; i++) {
        pci_device_t dev;
        if (!pci_get_device(i, &dev)) continue;

        driver_probe_ctx_t ctx = {
            .bus_type = DRIVER_BUS_PCI,
            .vendor_id = dev.vendor_id,
            .device_id = dev.device_id,
            .class_code = dev.class_code,
            .subclass = dev.subclass,
            .prog_if = dev.prog_if,
            .pci_bus = dev.bus,
            .pci_slot = dev.slot,
            .pci_function = dev.function
        };
        (void)driver_probe_context(&ctx);
    }

    serial_write("[DRV] Total bindings: ");
    serial_write_dec(g_driver_model.binding_count);
    serial_write("\n");
}

uint16_t driver_get_binding_count(void) {
    return g_driver_model.binding_count;
}

bool driver_get_binding(uint16_t index, driver_binding_t* out_binding) {
    if (!out_binding) return false;
    if (index >= g_driver_model.binding_count) return false;
    *out_binding = g_driver_model.bindings[index];
    return true;
}
