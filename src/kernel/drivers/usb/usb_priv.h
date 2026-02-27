#ifndef _KERNEL_USB_PRIV_H
#define _KERNEL_USB_PRIV_H

#include <drivers/usb.h>

#include <drivers/pci.h>
#include <drivers/serial.h>
#include <drivers/pit.h>
#include <drivers/keyboard.h>
#include <drivers/mouse.h>
#include <kernel/io.h>
#include <mm/pmm.h>
#include <mm/vmm.h>
#include <kernel/string.h>

#define PCI_CLASS_SERIAL_BUS 0x0Cu
#define PCI_SUBCLASS_USB     0x03u
#define USB_MMIO_LIMIT_4G    0x100000000ULL
#define USB_PROBE_POLL_LIMIT 100000u

#define USB_DIR_OUT          0x00u
#define USB_DIR_IN           0x80u
#define USB_TYPE_STANDARD    0x00u
#define USB_TYPE_CLASS       0x20u
#define USB_RECIP_DEVICE     0x00u
#define USB_RECIP_INTERFACE  0x01u

#define USB_REQ_GET_DESCRIPTOR 0x06u
#define USB_REQ_SET_ADDRESS    0x05u
#define USB_REQ_SET_CONFIGURATION 0x09u
#define USB_REQ_SET_IDLE       0x0Au

#define USB_DESC_DEVICE     0x01u
#define USB_DESC_CONFIG     0x02u
#define USB_DESC_INTERFACE  0x04u
#define USB_DESC_ENDPOINT   0x05u

#define USB_HID_CLASS       0x03u
#define USB_HID_SUBCLASS_BOOT 0x01u
#define USB_HID_PROTO_KEYBOARD 0x01u
#define USB_HID_PROTO_MOUSE    0x02u

#define UHCI_REG_USBCMD      0x00u
#define UHCI_REG_USBSTS      0x02u
#define UHCI_REG_USBINTR     0x04u
#define UHCI_REG_FRNUM       0x06u
#define UHCI_REG_FLBASEADD   0x08u
#define UHCI_REG_SOFMOD      0x0Cu
#define UHCI_REG_PORTSC1     0x10u
#define UHCI_PORT_STRIDE     0x02u
#define UHCI_MAX_PORTS       2u
#define UHCI_USBCMD_RS       (1u << 0)
#define UHCI_USBCMD_HCRESET  (1u << 1)
#define UHCI_USBCMD_CF       (1u << 6)
#define UHCI_USBSTS_HCHALTED (1u << 5)
#define UHCI_PORTSC_CCS      (1u << 0)
#define UHCI_PORTSC_PED      (1u << 2)
#define UHCI_PORTSC_LSDA     (1u << 8)
#define UHCI_PORTSC_PR       (1u << 9)
#define UHCI_PORTSC_CSC      (1u << 1)
#define UHCI_PORTSC_PEDC     (1u << 3)

#define UHCI_LINK_TERMINATE  0x00000001u
#define UHCI_LINK_QH         0x00000002u

#define UHCI_TD_STATUS_ACTIVE   (1u << 23)
#define UHCI_TD_STATUS_STALLED  (1u << 22)
#define UHCI_TD_STATUS_DBUFERR  (1u << 21)
#define UHCI_TD_STATUS_BABBLE   (1u << 20)
#define UHCI_TD_STATUS_NAK      (1u << 19)
#define UHCI_TD_STATUS_CRC      (1u << 18)
#define UHCI_TD_STATUS_BITSTUFF (1u << 17)
#define UHCI_TD_STATUS_SPD      (1u << 29)
#define UHCI_TD_CTRL_CERR       (3u << 27)
#define UHCI_TD_CTRL_LS         (1u << 26)

#define UHCI_PID_OUT   0xE1u
#define UHCI_PID_IN    0x69u
#define UHCI_PID_SETUP 0x2Du

#define OHCI_REG_CONTROL      0x04u
#define OHCI_REG_CMDSTATUS    0x08u
#define OHCI_REG_RHDESCA      0x48u
#define OHCI_REG_RHPORT_BASE  0x54u
#define OHCI_PORT_STRIDE      0x04u
#define OHCI_CMDSTATUS_HCR    (1u << 0)
#define OHCI_CONTROL_HCFS_MASK (3u << 6)
#define OHCI_CONTROL_HCFS_OPERATIONAL (2u << 6)
#define OHCI_RHPORT_CCS       (1u << 0)
#define OHCI_RHPORT_LSDA      (1u << 9)

#define EHCI_CAP_CAPLENGTH    0x00u
#define EHCI_CAP_HCSPARAMS    0x04u
#define EHCI_CAP_HCCPARAMS    0x08u
#define EHCI_OP_USBCMD        0x00u
#define EHCI_OP_USBSTS        0x04u
#define EHCI_OP_USBINTR       0x08u
#define EHCI_OP_CONFIGFLAG    0x40u
#define EHCI_OP_PORTSC_BASE   0x44u
#define EHCI_OP_PORTSC_STRIDE 0x04u
#define EHCI_USBCMD_RUN       (1u << 0)
#define EHCI_USBCMD_HCRESET   (1u << 1)
#define EHCI_USBSTS_HCHALTED  (1u << 12)
#define EHCI_PORTSC_CCS       (1u << 0)
#define EHCI_PORTSC_OWNER     (1u << 13)
#define EHCI_LEGSUP_BIOS_OWNED (1u << 16)
#define EHCI_LEGSUP_OS_OWNED   (1u << 24)

#define XHCI_CAP_CAPLENGTH   0x00u
#define XHCI_CAP_HCSPARAMS1  0x04u
#define XHCI_CAP_HCCPARAMS1  0x10u

#define XHCI_OP_USBSTS       0x04u
#define XHCI_OP_PORTSC_BASE  0x400u
#define XHCI_OP_PORTSC_STRIDE 0x10u

#define XHCI_USBSTS_CNR      (1u << 11)
#define XHCI_PORTSC_CCS      (1u << 0)
#define XHCI_PORTSC_SPEED_SHIFT 10u
#define XHCI_PORTSC_SPEED_MASK  (0xFu << XHCI_PORTSC_SPEED_SHIFT)
#define XHCI_EXT_CAP_ID_LEGACY 0x01u
#define XHCI_LEGSUP_BIOS_OWNED (1u << 16)
#define XHCI_LEGSUP_OS_OWNED   (1u << 24)

#define UHCI_TD_POOL_COUNT 64u
#define UHCI_DATA_POOL_SIZE 2048u
#define USB_HID_MAX_DEVICES 8u
#define USB_HANDOFF_POLL_LIMIT 10000u

#define USB_HID_TYPE_NONE     0u
#define USB_HID_TYPE_KEYBOARD 1u
#define USB_HID_TYPE_MOUSE    2u
#define USB_KBD_REPEAT_DELAY_TICKS 35u
#define USB_KBD_REPEAT_RATE_TICKS  5u

typedef struct __attribute__((packed, aligned(16))) {
    volatile uint32_t link_ptr;
    volatile uint32_t ctrl_status;
    volatile uint32_t token;
    volatile uint32_t buffer_ptr;
} uhci_td_t;

typedef struct __attribute__((packed, aligned(16))) {
    volatile uint32_t head_ptr;
    volatile uint32_t element_ptr;
} uhci_qh_t;

typedef struct __attribute__((packed)) {
    uint8_t bmRequestType;
    uint8_t bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
} usb_setup_packet_t;

typedef struct __attribute__((packed)) {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint16_t bcdUSB;
    uint8_t bDeviceClass;
    uint8_t bDeviceSubClass;
    uint8_t bDeviceProtocol;
    uint8_t bMaxPacketSize0;
    uint16_t idVendor;
    uint16_t idProduct;
    uint16_t bcdDevice;
    uint8_t iManufacturer;
    uint8_t iProduct;
    uint8_t iSerialNumber;
    uint8_t bNumConfigurations;
} usb_device_descriptor_t;

typedef struct __attribute__((packed)) {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint16_t wTotalLength;
    uint8_t bNumInterfaces;
    uint8_t bConfigurationValue;
    uint8_t iConfiguration;
    uint8_t bmAttributes;
    uint8_t bMaxPower;
} usb_config_descriptor_t;

typedef struct __attribute__((packed)) {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint8_t bInterfaceNumber;
    uint8_t bAlternateSetting;
    uint8_t bNumEndpoints;
    uint8_t bInterfaceClass;
    uint8_t bInterfaceSubClass;
    uint8_t bInterfaceProtocol;
    uint8_t iInterface;
} usb_interface_descriptor_t;

typedef struct __attribute__((packed)) {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint8_t bEndpointAddress;
    uint8_t bmAttributes;
    uint16_t wMaxPacketSize;
    uint8_t bInterval;
} usb_endpoint_descriptor_t;

typedef struct {
    bool ready;
    uint16_t io_base;
    uint64_t pages_phys;
    uint8_t* pages_virt;
    volatile uint32_t* frame_list;
    volatile uhci_qh_t* qh;
    volatile uhci_td_t* tds;
    uint8_t* data_pool;
    uint64_t qh_phys;
    uint64_t tds_phys;
    uint64_t data_phys;
} uhci_host_t;

typedef struct {
    bool used;
    uint8_t type;
    uint8_t controller_index;
    uint8_t port_number;
    uint8_t address;
    uint8_t endpoint_in;
    uint8_t max_packet;
    uint8_t interval;
    bool low_speed;
    bool toggle;
    bool caps_lock;
    uint8_t last_report[8];
    uint8_t repeat_usage;
    uint64_t repeat_next_tick;
    uint64_t interval_ticks;
    uint64_t next_poll_tick;
} usb_hid_device_t;

extern usb_controller_info_t g_usb_controllers[USB_MAX_CONTROLLERS];
extern uint16_t g_usb_count;
extern bool g_usb_scanned;

extern uhci_host_t g_uhci_hosts[USB_MAX_CONTROLLERS];
extern usb_hid_device_t g_hid_devices[USB_HID_MAX_DEVICES];
extern uint8_t g_next_usb_address;

uint8_t usb_mmio_read8(uint64_t base, uint32_t offset);
uint32_t usb_mmio_read32(uint64_t base, uint32_t offset);
void usb_mmio_write32(uint64_t base, uint32_t offset, uint32_t value);

bool usb_wait_io16_clear(uint16_t port, uint16_t mask, uint32_t poll_limit);
bool usb_wait_io16_set(uint16_t port, uint16_t mask, uint32_t poll_limit);
bool usb_wait_mmio32_clear(uint64_t base, uint32_t offset, uint32_t mask, uint32_t poll_limit);
bool usb_wait_mmio32_set(uint64_t base, uint32_t offset, uint32_t mask, uint32_t poll_limit);
void usb_delay_us(uint32_t us);
void usb_delay_ms(uint32_t ms);

uint8_t usb_controller_type_from_prog_if(uint8_t prog_if);
const char* usb_xhci_speed_name(uint8_t speed_code);
const char* usb_full_or_low_speed_name(bool low_speed);
void usb_parse_resources(uint8_t bus, uint8_t slot, uint8_t function,
                         uint32_t* out_io_base, uint64_t* out_mmio_base);
void usb_log_controller(const usb_controller_info_t* info);
void usb_ehci_legacy_handoff(const pci_device_t* dev, uint64_t mmio_virt);
void usb_xhci_legacy_handoff(const pci_device_t* dev, uint64_t mmio_virt);

bool usb_probe_uhci(const pci_device_t* dev, usb_controller_info_t* info, uint8_t controller_index);
bool usb_probe_ohci(const pci_device_t* dev, usb_controller_info_t* info);
bool usb_probe_ehci(const pci_device_t* dev, usb_controller_info_t* info);
bool usb_probe_xhci(const pci_device_t* dev, usb_controller_info_t* info);

#endif
