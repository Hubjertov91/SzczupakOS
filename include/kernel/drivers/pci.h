#ifndef _KERNEL_DRIVERS_PCI_H
#define _KERNEL_DRIVERS_PCI_H

#include <kernel/stdint.h>

#define PCI_MAX_DEVICES 256u

#define PCI_COMMAND_IO_SPACE     0x0001u
#define PCI_COMMAND_MEMORY_SPACE 0x0002u
#define PCI_COMMAND_BUS_MASTER   0x0004u

typedef struct {
    uint8_t bus;
    uint8_t slot;
    uint8_t function;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;
    uint8_t revision_id;
    uint8_t header_type;
    uint16_t vendor_id;
    uint16_t device_id;
    bool is_pcie;
} pci_device_t;

bool pci_init(void);
uint16_t pci_get_device_count(void);
bool pci_get_device(uint16_t index, pci_device_t* out_device);
bool pci_find_device(uint16_t vendor_id, uint16_t device_id, pci_device_t* out_device);

uint8_t pci_read8(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset);
uint16_t pci_read16(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset);
uint32_t pci_read32(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset);
void pci_write8(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset, uint8_t value);
void pci_write16(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset, uint16_t value);
void pci_write32(uint8_t bus, uint8_t slot, uint8_t function, uint8_t offset, uint32_t value);

bool pci_set_command_bits(uint8_t bus, uint8_t slot, uint8_t function, uint16_t bits);
uint32_t pci_read_bar(uint8_t bus, uint8_t slot, uint8_t function, uint8_t bar_index);

#endif
