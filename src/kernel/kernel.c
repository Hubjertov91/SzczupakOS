#include <kernel/vga.h>
#include <kernel/drivers/serial.h>
#include <kernel/multiboot2.h>
#include <kernel/mm/heap.h>
#include <kernel/mm/vmm.h>
#include <kernel/mm/pagefault.h>
#include <kernel/arch/idt.h>
#include <kernel/arch/gdt.h>
#include <kernel/task/syscall.h>
#include <kernel/drivers/pic.h>
#include <kernel/drivers/pit.h>
#include <kernel/drivers/keyboard.h>
#include <kernel/drivers/mouse.h>
#include <kernel/task/scheduler.h>
#include <kernel/task/task.h>
#include <kernel/task/tss.h>
#include <kernel/fs/vfs.h>
#include <kernel/fs/fat16.h>
#include <kernel/elf.h>
#include <kernel/drivers/framebuffer.h>
#include <kernel/drivers/psf.h>
#include <net/net.h>

#define KERNEL_STACK_SIZE 16384
static const uint8_t FALLBACK_STATIC_IP[4] = {192, 168, 76, 2};
static const uint8_t FALLBACK_STATIC_MASK[4] = {255, 255, 255, 0};
static const uint8_t FALLBACK_STATIC_GW[4] = {192, 168, 76, 1};
static const uint8_t FALLBACK_STATIC_DNS[4] = {1, 1, 1, 1};

void kernel_main(uint64_t multiboot_addr) {
    vga_init();
    serial_init();
    
    serial_write("[KERNEL] Starting SzczupakOS...\n");
    
    if (!multiboot_parse(multiboot_addr)) {
        serial_write("[KERNEL] ERROR: Failed to parse multiboot info\n");
        while(1) __asm__ volatile("hlt");
    }
    
    if (!heap_init()) {
        serial_write("[KERNEL] ERROR: Heap initialization failed\n");
        while(1) __asm__ volatile("hlt");
    }
    
    if (!vmm_init()) {
        serial_write("[KERNEL] ERROR: Virtual memory initialization failed\n");
        while(1) __asm__ volatile("hlt");
    }
    
    struct multiboot_tag_framebuffer* fb_tag = multiboot_get_framebuffer_tag();
    if (fb_tag) {
        if (!framebuffer_init(fb_tag)) {
            serial_write("[KERNEL] WARNING: Framebuffer initialization failed\n");
        }
    }
    
    if (!pagefault_init()) {
        serial_write("[KERNEL] ERROR: Page fault handler initialization failed\n");
        while(1) __asm__ volatile("hlt");
    }
    
    idt_init();
    gdt_init();
    
    if (!syscall_init()) {
        serial_write("[KERNEL] ERROR: Syscall initialization failed\n");
        while(1) __asm__ volatile("hlt");
    }
    
    pic_init();
    pit_init(100);
    keyboard_init();
    mouse_init();

    bool net_ready = false;
    if (net_init()) {
        __asm__ volatile("sti");
        if (!net_configure_dhcp(8000)) {
            serial_write("[KERNEL] WARNING: DHCP configuration failed, switching to static 192.168.76.2\n");
            if (!net_configure_static(FALLBACK_STATIC_IP, FALLBACK_STATIC_MASK,
                                      FALLBACK_STATIC_GW, FALLBACK_STATIC_DNS)) {
                serial_write("[KERNEL] WARNING: Static network fallback failed\n");
            }
        }
        __asm__ volatile("cli");
        net_ready = net_is_ready();
    } else {
        serial_write("[KERNEL] WARNING: Network initialization failed\n");
    }
    
    if (!task_init()) {
        serial_write("[KERNEL] ERROR: Task initialization failed\n");
        while(1) __asm__ volatile("hlt");
    }
    
    scheduler_init();
    vfs_init();

    if (framebuffer_available()) {
        fb_color_t bg = {0, 0, 0, 0};
        fb_clear(bg);
    }

    if (!fat16_mount(0)) {
        serial_write("[KERNEL] ERROR: Failed to mount FAT16 filesystem\n");
        while(1) __asm__ volatile("hlt");
    }
    
    vfs_filesystem_t* fat = fat16_create();
    if (!fat) {
        serial_write("[KERNEL] ERROR: Failed to create FAT16 VFS\n");
        while(1) __asm__ volatile("hlt");
    }
    
    if (!vfs_mount(fat, "/")) {
        serial_write("[KERNEL] ERROR: Failed to mount FAT16 VFS\n");
        while(1) __asm__ volatile("hlt");
    }

    if (!psf_load("/FONT.PSF")) {
        serial_write("[KERNEL] WARNING: Failed to load font\n");
    }

    vfs_node_t* shell_file = vfs_open("/SHELL.ELF", 0);
    if (!shell_file) {
        serial_write("[KERNEL] ERROR: Failed to open shell executable\n");
        while(1) __asm__ volatile("hlt");
    }

    if (shell_file->size == 0) {
        serial_write("[KERNEL] ERROR: Shell executable has zero size\n");
        vfs_close(shell_file);
        while(1) __asm__ volatile("hlt");
    }

    uint8_t* elf_data = kmalloc(shell_file->size);
    if (!elf_data) {
        serial_write("[KERNEL] ERROR: Failed to allocate memory for shell\n");
        vfs_close(shell_file);
        while(1) __asm__ volatile("hlt");
    }

    if (!vfs_read(shell_file, elf_data, 0, shell_file->size)) {
        serial_write("[KERNEL] ERROR: Failed to read shell executable\n");
        kfree(elf_data);
        vfs_close(shell_file);
        while(1) __asm__ volatile("hlt");
    }

    task_t* task = task_create_user("shell", "/SHELL.ELF", elf_data, shell_file->size);
    if (!task) {
        serial_write("[KERNEL] ERROR: Failed to create shell task\n");
        kfree(elf_data);
        vfs_close(shell_file);
        while(1) __asm__ volatile("hlt");
    }

    kfree(elf_data);
    vfs_close(shell_file);

    if (!task->kernel_stack) {
        serial_write("[KERNEL] ERROR: Shell task has no kernel stack\n");
        while(1) __asm__ volatile("hlt");
    }

    tss_set_kernel_stack((uint64_t)task->kernel_stack + KERNEL_STACK_SIZE);

    scheduler_enable();
    __asm__ volatile("sti");

    serial_write("[KERNEL] System ready, entering idle loop\n");
    
    while (1) {
        if (net_ready) {
            net_poll();
        }
        __asm__ volatile("hlt");
    }
}
