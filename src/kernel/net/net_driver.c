#include <mm/vmm.h>

typedef struct {
    const char* name;
    bool (*send_frame)(const uint8_t* frame, size_t length);
    bool (*poll_frame)(uint8_t* out, size_t out_capacity, size_t* out_length);
    bool (*link_up)(void);
} net_backend_ops_t;

static const net_backend_ops_t* g_net_backend_ops = NULL;
static char g_net_backend_name[16] = "none";

static void net_backend_set(const net_backend_ops_t* ops, const uint8_t mac[6]) {
    g_net_backend_ops = ops;
    g_rtl8139.initialized = (ops != NULL);

    if (mac) {
        memcpy(g_rtl8139.mac, mac, 6);
    } else {
        memset(g_rtl8139.mac, 0, 6);
    }

    memset(g_net_backend_name, 0, sizeof(g_net_backend_name));
    if (ops && ops->name) {
        strncpy(g_net_backend_name, ops->name, sizeof(g_net_backend_name) - 1u);
    } else {
        strncpy(g_net_backend_name, "none", sizeof(g_net_backend_name) - 1u);
    }
}

static bool net_driver_link_up(void) {
    if (!g_net_backend_ops || !g_rtl8139.initialized) {
        return false;
    }
    if (!g_net_backend_ops->link_up) {
        return true;
    }
    return g_net_backend_ops->link_up();
}

static const char* net_driver_backend_name(void) {
    return g_net_backend_name;
}

static uint64_t net_driver_phys_addr(const void* ptr) {
    if (!ptr) {
        return 0;
    }

    uint64_t va = (uint64_t)ptr;
    if (va < 0x100000000ULL) {
        return va;
    }

    page_directory_t* kernel_dir = vmm_get_kernel_directory();
    if (kernel_dir) {
        uint64_t pa = vmm_get_physical(kernel_dir, va);
        if (pa != 0u) {
            return pa;
        }
    }

    if (va >= 0xFFFF800000000000ULL) {
        return VIRT_TO_PHYS(va);
    }

    return 0;
}

static bool rtl8139_find(uint8_t* out_bus, uint8_t* out_slot, uint8_t* out_function) {
    if (!out_bus || !out_slot || !out_function) {
        return false;
    }

    pci_device_t dev;
    if (pci_find_device(RTL8139_VENDOR_ID, RTL8139_DEVICE_ID, &dev)) {
        *out_bus = dev.bus;
        *out_slot = dev.slot;
        *out_function = dev.function;
        return true;
    }

    return false;
}

static bool rtl8139_hw_init(void) {
    uint8_t bus = 0;
    uint8_t slot = 0;
    uint8_t function = 0;

    if (!rtl8139_find(&bus, &slot, &function)) {
        return false;
    }

    (void)pci_set_command_bits(bus, slot, function,
                               (uint16_t)(PCI_COMMAND_IO_SPACE | PCI_COMMAND_BUS_MASTER));

    uint32_t bar0 = pci_read_bar(bus, slot, function, 0u);
    if ((bar0 & 0x1u) == 0u) {
        serial_write("[NET] RTL8139 BAR0 is not I/O mapped\n");
        return false;
    }

    uint16_t io_base = (uint16_t)(bar0 & 0xFFFCu);

    memset(&g_rtl8139, 0, sizeof(g_rtl8139));
    g_rtl8139.io_base = io_base;

    outb((uint16_t)(io_base + RTL_CONFIG1), 0x00);

    outb((uint16_t)(io_base + RTL_CR), RTL_CR_RESET);
    for (uint32_t i = 0; i < 200000u; i++) {
        if ((inb((uint16_t)(io_base + RTL_CR)) & RTL_CR_RESET) == 0u) {
            break;
        }
        __asm__ volatile("pause");
    }
    if ((inb((uint16_t)(io_base + RTL_CR)) & RTL_CR_RESET) != 0u) {
        serial_write("[NET] RTL8139 reset timed out\n");
        return false;
    }

    for (uint8_t i = 0; i < 6u; i++) {
        g_rtl8139.mac[i] = inb((uint16_t)(io_base + RTL_IDR0 + i));
    }

    uint64_t rx_addr64 = net_driver_phys_addr(&g_rtl8139_rx_buffer[0]);
    if (rx_addr64 == 0u || rx_addr64 > 0xFFFFFFFFu) {
        serial_write("[NET] RTL8139 RX buffer above 4GB\n");
        return false;
    }

    outl((uint16_t)(io_base + RTL_RBSTART), (uint32_t)rx_addr64);

    for (uint8_t i = 0; i < RTL_TX_BUFFER_COUNT; i++) {
        uint64_t tx_addr64 = net_driver_phys_addr(&g_rtl8139_tx_buffers[i][0]);
        if (tx_addr64 == 0u || tx_addr64 > 0xFFFFFFFFu) {
            serial_write("[NET] RTL8139 TX buffer above 4GB\n");
            return false;
        }
        outl((uint16_t)(io_base + RTL_TSAD0 + (i * 4u)), (uint32_t)tx_addr64);
    }

    outw((uint16_t)(io_base + RTL_IMR), 0x0000u);
    outw((uint16_t)(io_base + RTL_ISR), 0xFFFFu);

    outl((uint16_t)(io_base + RTL_RCR), 0x0000000Fu | (1u << 7));
    outl((uint16_t)(io_base + RTL_TCR), 0x03000700u);

    outb((uint16_t)(io_base + RTL_CR), RTL_CR_TX_ENABLE | RTL_CR_RX_ENABLE);

    g_rtl8139.rx_offset = 0;
    g_rtl8139.tx_index = 0;
    g_rtl8139.initialized = true;

    serial_write("[NET] RTL8139 initialized, MAC ");
    serial_write_mac(g_rtl8139.mac);
    serial_write("\n");

    return true;
}

static bool rtl8139_hw_send_frame(const uint8_t* frame, size_t length) {
    if (!g_rtl8139.initialized || !frame || length == 0) {
        return false;
    }

    if (length > RTL_TX_BUFFER_SIZE) {
        return false;
    }

    uint8_t index = g_rtl8139.tx_index;
    uint8_t* tx_buffer = g_rtl8139_tx_buffers[index];

    size_t tx_length = (length < 60u) ? 60u : length;
    memcpy(tx_buffer, frame, length);
    if (tx_length > length) {
        memset(tx_buffer + length, 0, tx_length - length);
    }

    uint16_t io_base = g_rtl8139.io_base;
    outl((uint16_t)(io_base + RTL_TSD0 + (index * 4u)), (uint32_t)tx_length);

    uint32_t status = 0;
    for (uint32_t i = 0; i < 100000u; i++) {
        status = inl((uint16_t)(io_base + RTL_TSD0 + (index * 4u)));
        if ((status & (RTL_TSD_TOK | RTL_TSD_TUN | RTL_TSD_TABT | RTL_TSD_OWC)) != 0u) {
            break;
        }
        __asm__ volatile("pause");
    }

    g_rtl8139.tx_index = (uint8_t)((index + 1u) % RTL_TX_BUFFER_COUNT);
    return (status & RTL_TSD_TOK) != 0u;
}

static uint8_t rtl8139_ring_read_u8(uint32_t offset) {
    return g_rtl8139_rx_buffer[offset % RTL_RX_BUFFER_SIZE];
}

static uint16_t rtl8139_ring_read_u16(uint32_t offset) {
    uint16_t low = rtl8139_ring_read_u8(offset);
    uint16_t high = rtl8139_ring_read_u8(offset + 1u);
    return (uint16_t)(low | (high << 8));
}

static void rtl8139_ring_copy(uint32_t offset, uint8_t* out, size_t length) {
    if (!out || length == 0) {
        return;
    }
    for (size_t i = 0; i < length; i++) {
        out[i] = rtl8139_ring_read_u8(offset + (uint32_t)i);
    }
}

static bool rtl8139_hw_poll_frame(uint8_t* out, size_t out_capacity, size_t* out_length) {
    if (!g_rtl8139.initialized || !out || !out_length || out_capacity == 0) {
        return false;
    }

    uint16_t io_base = g_rtl8139.io_base;
    if ((inb((uint16_t)(io_base + RTL_CR)) & RTL_CR_RX_EMPTY) != 0u) {
        return false;
    }

    uint32_t offset = g_rtl8139.rx_offset;
    uint16_t status = rtl8139_ring_read_u16(offset);
    uint16_t packet_length_with_crc = rtl8139_ring_read_u16(offset + 2u);

    if ((status & 0x0001u) == 0u || packet_length_with_crc < 4u || packet_length_with_crc > 1792u) {
        g_rtl8139.rx_offset = 0;
        outw((uint16_t)(io_base + RTL_CAPR), 0x0000u);
        outw((uint16_t)(io_base + RTL_ISR), 0xFFFFu);
        return false;
    }

    size_t packet_length = (size_t)(packet_length_with_crc - 4u);
    if (packet_length > out_capacity) {
        packet_length = out_capacity;
    }

    rtl8139_ring_copy(offset + 4u, out, packet_length);

    uint32_t advanced = offset + (uint32_t)packet_length_with_crc + 4u;
    advanced = (advanced + 3u) & ~3u;
    g_rtl8139.rx_offset = advanced % RTL_RX_BUFFER_SIZE;

    uint16_t capr = (uint16_t)((g_rtl8139.rx_offset - 16u) & 0xFFFFu);
    outw((uint16_t)(io_base + RTL_CAPR), capr);
    outw((uint16_t)(io_base + RTL_ISR), 0x0001u);

    *out_length = packet_length;
    return true;
}

static bool rtl8139_hw_link_up(void) {
    return g_rtl8139.initialized;
}

#define E1000_VENDOR_ID 0x8086u

#define E1000_REG_CTRL   0x0000u
#define E1000_REG_STATUS 0x0008u
#define E1000_REG_EERD   0x0014u
#define E1000_REG_ICR    0x00C0u
#define E1000_REG_RCTL   0x0100u
#define E1000_REG_TCTL   0x0400u
#define E1000_REG_TIPG   0x0410u
#define E1000_REG_RDBAL  0x2800u
#define E1000_REG_RDBAH  0x2804u
#define E1000_REG_RDLEN  0x2808u
#define E1000_REG_RDH    0x2810u
#define E1000_REG_RDT    0x2818u
#define E1000_REG_TDBAL  0x3800u
#define E1000_REG_TDBAH  0x3804u
#define E1000_REG_TDLEN  0x3808u
#define E1000_REG_TDH    0x3810u
#define E1000_REG_TDT    0x3818u
#define E1000_REG_RAL0   0x5400u
#define E1000_REG_RAH0   0x5404u

#define E1000_CTRL_RST (1u << 26)
#define E1000_STATUS_LU (1u << 1)

#define E1000_RCTL_EN     0x00000002u
#define E1000_RCTL_UPE    0x00000008u
#define E1000_RCTL_MPE    0x00000010u
#define E1000_RCTL_BAM    0x00008000u
#define E1000_RCTL_SECRC  0x04000000u

#define E1000_TCTL_EN     0x00000002u
#define E1000_TCTL_PSP    0x00000008u

#define E1000_TXD_CMD_EOP  0x01u
#define E1000_TXD_CMD_IFCS 0x02u
#define E1000_TXD_CMD_RS   0x08u
#define E1000_TXD_STAT_DD  0x01u

#define E1000_RXD_STAT_DD  0x01u

#define E1000_MMIO_MAP_BASE 0xFFFF800230000000ULL
#define E1000_MMIO_MAP_SIZE 0x10000u

#define E1000_RX_DESC_COUNT 32u
#define E1000_TX_DESC_COUNT 32u
#define E1000_RX_BUF_SIZE   2048u
#define E1000_TX_BUF_SIZE   2048u

typedef struct __attribute__((packed)) {
    uint64_t addr;
    uint16_t length;
    uint16_t checksum;
    uint8_t status;
    uint8_t errors;
    uint16_t special;
} e1000_rx_desc_t;

typedef struct __attribute__((packed)) {
    uint64_t addr;
    uint16_t length;
    uint8_t cso;
    uint8_t cmd;
    uint8_t status;
    uint8_t css;
    uint16_t special;
} e1000_tx_desc_t;

typedef struct {
    bool initialized;
    uint8_t bus;
    uint8_t slot;
    uint8_t function;
    volatile uint8_t* mmio;
    uint8_t mac[6];
    uint16_t rx_head;
    uint16_t tx_tail;
} e1000_state_t;

static e1000_state_t g_e1000;
static e1000_rx_desc_t g_e1000_rx_desc[E1000_RX_DESC_COUNT] __attribute__((aligned(16)));
static e1000_tx_desc_t g_e1000_tx_desc[E1000_TX_DESC_COUNT] __attribute__((aligned(16)));
static uint8_t g_e1000_rx_buf[E1000_RX_DESC_COUNT][E1000_RX_BUF_SIZE] __attribute__((aligned(16)));
static uint8_t g_e1000_tx_buf[E1000_TX_DESC_COUNT][E1000_TX_BUF_SIZE] __attribute__((aligned(16)));

static uint32_t e1000_mmio_read(uint32_t reg) {
    volatile uint32_t* ptr = (volatile uint32_t*)(g_e1000.mmio + reg);
    return *ptr;
}

static void e1000_mmio_write(uint32_t reg, uint32_t value) {
    volatile uint32_t* ptr = (volatile uint32_t*)(g_e1000.mmio + reg);
    *ptr = value;
    (void)e1000_mmio_read(E1000_REG_STATUS);
}

static bool e1000_map_mmio(uint64_t bar_phys) {
    page_directory_t* kernel_dir = vmm_get_kernel_directory();
    if (!kernel_dir) {
        return false;
    }

    uint64_t phys_base = bar_phys & ~0xFFFULL;
    uint64_t offset = bar_phys - phys_base;
    size_t map_size = E1000_MMIO_MAP_SIZE + (size_t)offset;
    size_t pages = (map_size + PAGE_SIZE - 1u) / PAGE_SIZE;

    for (size_t i = 0; i < pages; i++) {
        uint64_t virt = E1000_MMIO_MAP_BASE + (i * PAGE_SIZE);
        uint64_t phys = phys_base + (i * PAGE_SIZE);
        if (!vmm_map_page(kernel_dir, virt, phys, PAGE_PRESENT | PAGE_WRITE)) {
            return false;
        }
    }

    g_e1000.mmio = (volatile uint8_t*)(E1000_MMIO_MAP_BASE + offset);
    return true;
}

static bool e1000_find(uint8_t* out_bus, uint8_t* out_slot, uint8_t* out_function, uint16_t* out_device_id) {
    if (!out_bus || !out_slot || !out_function) {
        return false;
    }

    static const uint16_t supported_ids[] = {
        0x100Eu, 0x100Fu, 0x1010u, 0x1011u, 0x1026u, 0x1027u, 0x1028u,
        0x107Cu, 0x107Du, 0x10D3u, 0x10F5u, 0x1502u, 0x153Au
    };

    pci_device_t dev;
    for (uint8_t i = 0u; i < (uint8_t)(sizeof(supported_ids) / sizeof(supported_ids[0])); i++) {
        if (pci_find_device(E1000_VENDOR_ID, supported_ids[i], &dev)) {
            *out_bus = dev.bus;
            *out_slot = dev.slot;
            *out_function = dev.function;
            if (out_device_id) {
                *out_device_id = dev.device_id;
            }
            return true;
        }
    }

    return false;
}

static bool e1000_wait_eerd_done(uint32_t* out_value) {
    for (uint32_t i = 0; i < 100000u; i++) {
        uint32_t value = e1000_mmio_read(E1000_REG_EERD);
        if ((value & (1u << 4)) != 0u) {
            if (out_value) {
                *out_value = value;
            }
            return true;
        }
        __asm__ volatile("pause");
    }
    return false;
}

static bool e1000_read_eeprom_word(uint8_t address, uint16_t* out_value) {
    if (!out_value) {
        return false;
    }

    uint32_t request = 1u | ((uint32_t)address << 8);
    e1000_mmio_write(E1000_REG_EERD, request);

    uint32_t value = 0;
    if (!e1000_wait_eerd_done(&value)) {
        return false;
    }

    *out_value = (uint16_t)(value >> 16);
    return true;
}

static bool e1000_read_mac(uint8_t out_mac[6]) {
    if (!out_mac) {
        return false;
    }

    uint16_t w0 = 0, w1 = 0, w2 = 0;
    if (e1000_read_eeprom_word(0u, &w0) &&
        e1000_read_eeprom_word(1u, &w1) &&
        e1000_read_eeprom_word(2u, &w2)) {
        out_mac[0] = (uint8_t)(w0 & 0xFFu);
        out_mac[1] = (uint8_t)(w0 >> 8);
        out_mac[2] = (uint8_t)(w1 & 0xFFu);
        out_mac[3] = (uint8_t)(w1 >> 8);
        out_mac[4] = (uint8_t)(w2 & 0xFFu);
        out_mac[5] = (uint8_t)(w2 >> 8);
        return !mac_is_zero(out_mac);
    }

    uint32_t ral = e1000_mmio_read(E1000_REG_RAL0);
    uint32_t rah = e1000_mmio_read(E1000_REG_RAH0);
    out_mac[0] = (uint8_t)(ral & 0xFFu);
    out_mac[1] = (uint8_t)((ral >> 8) & 0xFFu);
    out_mac[2] = (uint8_t)((ral >> 16) & 0xFFu);
    out_mac[3] = (uint8_t)((ral >> 24) & 0xFFu);
    out_mac[4] = (uint8_t)(rah & 0xFFu);
    out_mac[5] = (uint8_t)((rah >> 8) & 0xFFu);
    return !mac_is_zero(out_mac);
}

static bool e1000_setup_rx(void) {
    memset(g_e1000_rx_desc, 0, sizeof(g_e1000_rx_desc));
    memset(g_e1000_rx_buf, 0, sizeof(g_e1000_rx_buf));

    uint64_t desc_phys = net_driver_phys_addr(g_e1000_rx_desc);
    if (desc_phys == 0u) {
        return false;
    }

    for (uint16_t i = 0u; i < E1000_RX_DESC_COUNT; i++) {
        uint64_t buf_phys = net_driver_phys_addr(g_e1000_rx_buf[i]);
        if (buf_phys == 0u) {
            return false;
        }
        g_e1000_rx_desc[i].addr = buf_phys;
        g_e1000_rx_desc[i].status = 0u;
    }

    e1000_mmio_write(E1000_REG_RDBAL, (uint32_t)(desc_phys & 0xFFFFFFFFu));
    e1000_mmio_write(E1000_REG_RDBAH, (uint32_t)(desc_phys >> 32));
    e1000_mmio_write(E1000_REG_RDLEN, (uint32_t)(E1000_RX_DESC_COUNT * sizeof(e1000_rx_desc_t)));
    e1000_mmio_write(E1000_REG_RDH, 0u);
    e1000_mmio_write(E1000_REG_RDT, E1000_RX_DESC_COUNT - 1u);

    g_e1000.rx_head = 0u;

    uint32_t rctl = E1000_RCTL_EN | E1000_RCTL_UPE | E1000_RCTL_MPE |
                    E1000_RCTL_BAM | E1000_RCTL_SECRC;
    e1000_mmio_write(E1000_REG_RCTL, rctl);
    return true;
}

static bool e1000_setup_tx(void) {
    memset(g_e1000_tx_desc, 0, sizeof(g_e1000_tx_desc));
    memset(g_e1000_tx_buf, 0, sizeof(g_e1000_tx_buf));

    uint64_t desc_phys = net_driver_phys_addr(g_e1000_tx_desc);
    if (desc_phys == 0u) {
        return false;
    }

    for (uint16_t i = 0u; i < E1000_TX_DESC_COUNT; i++) {
        uint64_t buf_phys = net_driver_phys_addr(g_e1000_tx_buf[i]);
        if (buf_phys == 0u) {
            return false;
        }
        g_e1000_tx_desc[i].addr = buf_phys;
        g_e1000_tx_desc[i].status = E1000_TXD_STAT_DD;
    }

    e1000_mmio_write(E1000_REG_TDBAL, (uint32_t)(desc_phys & 0xFFFFFFFFu));
    e1000_mmio_write(E1000_REG_TDBAH, (uint32_t)(desc_phys >> 32));
    e1000_mmio_write(E1000_REG_TDLEN, (uint32_t)(E1000_TX_DESC_COUNT * sizeof(e1000_tx_desc_t)));
    e1000_mmio_write(E1000_REG_TDH, 0u);
    e1000_mmio_write(E1000_REG_TDT, 0u);
    g_e1000.tx_tail = 0u;

    uint32_t tctl = E1000_TCTL_EN | E1000_TCTL_PSP | (0x10u << 4) | (0x40u << 12);
    e1000_mmio_write(E1000_REG_TCTL, tctl);
    e1000_mmio_write(E1000_REG_TIPG, 0x0060200Au);
    return true;
}

static bool e1000_hw_init(void) {
    memset(&g_e1000, 0, sizeof(g_e1000));

    uint8_t bus = 0;
    uint8_t slot = 0;
    uint8_t function = 0;
    uint16_t device_id = 0u;
    if (!e1000_find(&bus, &slot, &function, &device_id)) {
        return false;
    }

    (void)pci_set_command_bits(bus, slot, function,
                               (uint16_t)(PCI_COMMAND_MEMORY_SPACE | PCI_COMMAND_BUS_MASTER));

    uint32_t bar0 = pci_read_bar(bus, slot, function, 0u);
    uint64_t mmio_phys = 0u;

    if ((bar0 & 0x1u) == 0u) {
        mmio_phys = (uint64_t)(bar0 & ~0xFu);
        if ((bar0 & 0x6u) == 0x4u) {
            uint32_t bar1 = pci_read_bar(bus, slot, function, 1u);
            mmio_phys |= ((uint64_t)bar1 << 32);
        }
    } else {
        uint32_t bar1 = pci_read_bar(bus, slot, function, 1u);
        if ((bar1 & 0x1u) == 0u) {
            mmio_phys = (uint64_t)(bar1 & ~0xFu);
        }
    }

    if (mmio_phys == 0u) {
        serial_write("[NET] E1000 MMIO BAR not found\n");
        return false;
    }

    if (!e1000_map_mmio(mmio_phys)) {
        serial_write("[NET] E1000 MMIO map failed\n");
        return false;
    }

    uint32_t status = e1000_mmio_read(E1000_REG_STATUS);
    if (status == 0xFFFFFFFFu) {
        serial_write("[NET] E1000 MMIO probe failed\n");
        return false;
    }

    uint32_t ctrl = e1000_mmio_read(E1000_REG_CTRL);
    e1000_mmio_write(E1000_REG_CTRL, ctrl | E1000_CTRL_RST);
    for (uint32_t i = 0; i < 200000u; i++) {
        if ((e1000_mmio_read(E1000_REG_CTRL) & E1000_CTRL_RST) == 0u) {
            break;
        }
        __asm__ volatile("pause");
    }

    (void)e1000_mmio_read(E1000_REG_ICR);

    uint8_t mac[6];
    if (!e1000_read_mac(mac)) {
        serial_write("[NET] E1000 failed to read MAC\n");
        return false;
    }
    memcpy(g_e1000.mac, mac, sizeof(g_e1000.mac));

    uint32_t ral = (uint32_t)mac[0] |
                   ((uint32_t)mac[1] << 8) |
                   ((uint32_t)mac[2] << 16) |
                   ((uint32_t)mac[3] << 24);
    uint32_t rah = (uint32_t)mac[4] |
                   ((uint32_t)mac[5] << 8) |
                   (1u << 31);
    e1000_mmio_write(E1000_REG_RAL0, ral);
    e1000_mmio_write(E1000_REG_RAH0, rah);

    if (!e1000_setup_rx() || !e1000_setup_tx()) {
        serial_write("[NET] E1000 descriptor setup failed\n");
        return false;
    }

    g_e1000.bus = bus;
    g_e1000.slot = slot;
    g_e1000.function = function;
    g_e1000.initialized = true;

    serial_write("[NET] E1000 initialized (device ");
    serial_write_hex((uint64_t)device_id);
    serial_write("), MAC ");
    serial_write_mac(g_e1000.mac);
    serial_write("\n");

    return true;
}

static bool e1000_hw_send_frame(const uint8_t* frame, size_t length) {
    if (!g_e1000.initialized || !frame || length == 0u || length > E1000_TX_BUF_SIZE) {
        return false;
    }

    uint16_t index = g_e1000.tx_tail;
    e1000_tx_desc_t* desc = &g_e1000_tx_desc[index];

    for (uint32_t i = 0; i < 100000u; i++) {
        if ((desc->status & E1000_TXD_STAT_DD) != 0u) {
            break;
        }
        __asm__ volatile("pause");
    }
    if ((desc->status & E1000_TXD_STAT_DD) == 0u) {
        return false;
    }

    size_t tx_length = (length < 60u) ? 60u : length;
    memcpy(g_e1000_tx_buf[index], frame, length);
    if (tx_length > length) {
        memset(g_e1000_tx_buf[index] + length, 0, tx_length - length);
    }

    desc->length = (uint16_t)tx_length;
    desc->cso = 0u;
    desc->cmd = E1000_TXD_CMD_EOP | E1000_TXD_CMD_IFCS | E1000_TXD_CMD_RS;
    desc->status = 0u;
    desc->css = 0u;
    desc->special = 0u;

    g_e1000.tx_tail = (uint16_t)((index + 1u) % E1000_TX_DESC_COUNT);
    e1000_mmio_write(E1000_REG_TDT, g_e1000.tx_tail);

    for (uint32_t i = 0; i < 100000u; i++) {
        if ((desc->status & E1000_TXD_STAT_DD) != 0u) {
            return true;
        }
        __asm__ volatile("pause");
    }

    return (desc->status & E1000_TXD_STAT_DD) != 0u;
}

static bool e1000_hw_poll_frame(uint8_t* out, size_t out_capacity, size_t* out_length) {
    if (!g_e1000.initialized || !out || !out_length || out_capacity == 0u) {
        return false;
    }

    uint16_t index = g_e1000.rx_head;
    e1000_rx_desc_t* desc = &g_e1000_rx_desc[index];
    if ((desc->status & E1000_RXD_STAT_DD) == 0u) {
        return false;
    }

    size_t frame_length = desc->length;
    if (frame_length > out_capacity) {
        frame_length = out_capacity;
    }

    if (frame_length > 0u) {
        memcpy(out, g_e1000_rx_buf[index], frame_length);
    }

    desc->status = 0u;
    e1000_mmio_write(E1000_REG_RDT, index);
    g_e1000.rx_head = (uint16_t)((index + 1u) % E1000_RX_DESC_COUNT);

    *out_length = frame_length;
    return frame_length > 0u;
}

static bool e1000_hw_link_up(void) {
    if (!g_e1000.initialized) {
        return false;
    }
    return (e1000_mmio_read(E1000_REG_STATUS) & E1000_STATUS_LU) != 0u;
}

static const net_backend_ops_t g_backend_rtl8139 = {
    .name = "rtl8139",
    .send_frame = rtl8139_hw_send_frame,
    .poll_frame = rtl8139_hw_poll_frame,
    .link_up = rtl8139_hw_link_up
};

static const net_backend_ops_t g_backend_e1000 = {
    .name = "e1000",
    .send_frame = e1000_hw_send_frame,
    .poll_frame = e1000_hw_poll_frame,
    .link_up = e1000_hw_link_up
};

static bool rtl8139_init(void) {
    net_backend_set(NULL, NULL);

    if (e1000_hw_init()) {
        net_backend_set(&g_backend_e1000, g_e1000.mac);
        serial_write("[NET] Backend selected: ");
        serial_write(net_driver_backend_name());
        serial_write("\n");
        return true;
    }

    if (rtl8139_hw_init()) {
        net_backend_set(&g_backend_rtl8139, g_rtl8139.mac);
        serial_write("[NET] Backend selected: ");
        serial_write(net_driver_backend_name());
        serial_write("\n");
        return true;
    }

    serial_write("[NET] No supported NIC backend found\n");
    return false;
}

static bool rtl8139_send_frame(const uint8_t* frame, size_t length) {
    if (!g_net_backend_ops || !g_net_backend_ops->send_frame) {
        return false;
    }
    return g_net_backend_ops->send_frame(frame, length);
}

static bool rtl8139_poll_frame(uint8_t* out, size_t out_capacity, size_t* out_length) {
    if (!g_net_backend_ops || !g_net_backend_ops->poll_frame) {
        return false;
    }
    return g_net_backend_ops->poll_frame(out, out_capacity, out_length);
}
