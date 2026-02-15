#include <kernel/vga.h>
#include <drivers/serial.h>
#include <kernel/multiboot2.h>
#include <mm/pmm.h>
#include <mm/vmm.h>
#include <mm/heap.h>
#include <arch/idt.h>
#include <task/task.h>
#include <task/scheduler.h>
#include <arch/gdt.h>
#include <fs/vfs.h>
#include <fs/fat16.h>
#include <kernel/elf.h>
#include <drivers/pic.h>
#include <drivers/pit.h>
#include <drivers/keyboard.h>
#include <task/syscall.h>
#include <mm/pagefault.h>
#include <task/tss.h>
#include <drivers/framebuffer.h>
#include <drivers/psf.h>

void kernel_main(uint64_t multiboot_addr) {
    vga_init();
    serial_init();
    
    multiboot_parse(multiboot_addr);
    heap_init();
    vmm_init();
    
    struct multiboot_tag_framebuffer* fb_tag = multiboot_get_framebuffer_tag();
    if (fb_tag) {
        framebuffer_init(fb_tag);
    }
    
    pagefault_init();
    idt_init();
    gdt_init();
    syscall_init();
    pic_init();
    pit_init(100);
    keyboard_init();
    task_init();
    scheduler_init();
    vfs_init();

    if (framebuffer_available()) {
        fb_color_t blue = {0, 100, 255, 255};
        fb_clear(blue);
    }

    if (!fat16_mount(0)) while(1);
    vfs_filesystem_t* fat = fat16_create();
    if (!fat) while(1);
    if (!vfs_mount(fat, "/")) while(1);

    psf_load("/FONT.PSF");

    vfs_node_t* shell_file = vfs_open("/SHELL.ELF", 0);
    if (!shell_file) while(1);

    uint8_t* elf_data = kmalloc(shell_file->size);
    if (!elf_data) while(1);

    if (!vfs_read(shell_file, elf_data, 0, shell_file->size)) while(1);

    task_t* task = task_create_user("shell", elf_data, shell_file->size);
    if (!task) while(1);

    kfree(elf_data);
    vfs_close(shell_file);

    tss_set_kernel_stack((uint64_t)task->kernel_stack + 8192);

    scheduler_enable();
    __asm__ volatile("sti");

    while (1) {
        __asm__ volatile("hlt");
    }
}