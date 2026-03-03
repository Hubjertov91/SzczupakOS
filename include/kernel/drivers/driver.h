#ifndef _KERNEL_DRIVERS_DRIVER_H
#define _KERNEL_DRIVERS_DRIVER_H

#include <kernel/stdint.h>

typedef enum {
    DRIVER_BUS_PLATFORM = 0,
    DRIVER_BUS_PCI = 1,
    DRIVER_BUS_USB = 2
} driver_bus_t;

#define DRIVER_MATCH_ANY_U16 0xFFFFu
#define DRIVER_MATCH_ANY_U8  0xFFu

typedef struct {
    driver_bus_t bus_type;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;
    uint8_t pci_bus;
    uint8_t pci_slot;
    uint8_t pci_function;
} driver_probe_ctx_t;

typedef bool (*driver_probe_fn)(const driver_probe_ctx_t* ctx);

typedef struct {
    const char* name;
    driver_bus_t bus_type;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;
    driver_probe_fn probe;
    uint8_t priority;
} driver_desc_t;

typedef struct {
    const char* driver_name;
    driver_probe_ctx_t ctx;
} driver_binding_t;

void driver_model_init(void);
bool driver_register(const driver_desc_t* driver);
void driver_register_builtin_descriptors(void);
void driver_reset_bindings(void);
bool driver_probe_context(const driver_probe_ctx_t* ctx);
void driver_probe_all(void);
uint16_t driver_get_binding_count(void);
bool driver_get_binding(uint16_t index, driver_binding_t* out_binding);

#endif
