#include <kernel/vga.h>
#include <kernel/serial.h>
#include <kernel/multiboot2.h>
#include <kernel/pmm.h>
#include <kernel/vmm.h>
#include <kernel/pagefault.h>
#include <kernel/heap.h>
#include <kernel/idt.h>
#include <drivers/pic.h>
#include <drivers/pit.h>
#include <drivers/keyboard.h>
#include <kernel/vfs.h>
#include <kernel/tmpfs.h>
#include <kernel/shell.h>
#include <kernel/task.h>
#include <kernel/scheduler.h>
#include <kernel/gdt.h>
#include <kernel/syscall.h>
#include <drivers/ata.h>
#include <kernel/fat16.h>
#include <kernel/elf.h>
#include <kernel/terminal.h>


void kernel_main(uint64_t multiboot_addr) {
    vga_init();
    serial_init();
    vga_write("OstrowekOS v0.5\nBooting...\n\n");
    
    multiboot_parse(multiboot_addr);

    terminal_init();
    heap_init();
    vmm_init();
    pagefault_init();
    vfs_init();

    idt_init();
    pic_init();
    pit_init(100);
    keyboard_init();

    ata_init();

    gdt_init();
    syscall_init();
    
    task_init();
    scheduler_init();

    task_t* shell_task = NULL;

    /* Filesystem and shell loading */
    if (!fat16_mount(0)) {
        serial_write("[KERNEL] WARNING: FAT16 mount failed\n");
    } else {
        vfs_filesystem_t* fat = fat16_create();
        if (!fat) {
            serial_write("[KERNEL] ERROR: FAT16 creation failed\n");
        } else if (!vfs_mount(fat, "/")) {
            serial_write("[KERNEL] ERROR: VFS mount failed\n");
        } else {
            serial_write("[KERNEL] FAT16 mounted at /\n");
            
            vfs_node_t* root = vfs_get_root();
            if (root) {
                serial_write("[KERNEL] Listing root directory:\n");
                
                vfs_node_t* child = root->first_child;
                while (child) {
                    serial_write("  - ");
                    serial_write(child->name);
                    serial_write("\n");
                    child = child->next_sibling;
                }
            }
            
            vfs_node_t* shell_file = vfs_open("/SHELL.ELF", 0);
            if (!shell_file) {
                serial_write("[KERNEL] WARNING: SHELL.ELF not found\n");
            } else {
                serial_write("[KERNEL] Found SHELL.ELF (size: 0x");
                serial_write_hex(shell_file->size);
                serial_write(")\n");
                
                uint8_t* elf_data = kmalloc(shell_file->size);
                if (!elf_data) {
                    serial_write("[KERNEL] ERROR: Memory allocation for ELF failed\n");
                } else {
                    bool success = vfs_read(shell_file, (void*)elf_data, 0, shell_file->size);
                    
                    if (!success) {
                        serial_write("[KERNEL] ERROR: Failed to read SHELL.ELF\n");
                    } else {
                        shell_task = task_create_user("shell", elf_data, shell_file->size);
                        if (shell_task) {
                            serial_write("[KERNEL] Task created: RIP=0x");
                            serial_write_hex(shell_task->context.rip);
                            serial_write(" RSP=0x");
                            serial_write_hex(shell_task->context.rsp);
                            serial_write(" CR3=0x");
                            serial_write_hex(shell_task->context.cr3);
                            serial_write("\n");
                        } else {
                            serial_write("[KERNEL] ERROR: Failed to create shell task\n");
                        }
                    }
                    
                    kfree(elf_data);
                }
                vfs_close(shell_file);
            }
        }
    }

    serial_write("[KERNEL] Enabling scheduler and interrupts...\n");
    scheduler_enable();
    __asm__ volatile("sti");

    while (1) {
        __asm__ volatile("hlt");
    }
}