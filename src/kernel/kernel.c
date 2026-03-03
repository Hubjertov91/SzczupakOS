#include <kernel/vga.h>
#include <kernel/drivers/serial.h>
#include <kernel/multiboot2.h>
#include <kernel/mm/heap.h>
#include <kernel/mm/vmm.h>
#include <kernel/mm/uaccess.h>
#include <kernel/mm/pagefault.h>
#include <kernel/arch/api.h>
#include <kernel/task/syscall.h>
#include <kernel/drivers/keyboard.h>
#include <kernel/drivers/mouse.h>
#include <kernel/drivers/rtc.h>
#include <kernel/drivers/pci.h>
#include <kernel/drivers/driver.h>
#include <kernel/drivers/usb.h>
#include <kernel/drivers/ata.h>
#include <kernel/drivers/rtl8168.h>
#include <kernel/task/scheduler.h>
#include <kernel/task/task.h>
#include <kernel/fs/vfs.h>
#include <kernel/fs/fat16.h>
#include <kernel/fs/tmpfs.h>
#include <kernel/elf.h>
#include <kernel/terminal.h>
#include <kernel/string.h>
#include <kernel/drivers/framebuffer.h>
#include <kernel/drivers/gpu.h>
#include <kernel/drivers/psf.h>
#include <kernel/drivers/apic.h>
#include <kernel/debug/panic.h>
#include <kernel/pty.h>
#include <kernel/firmware/acpi.h>
#include <net/net.h>

#define KERNEL_STACK_SIZE 16384
static const uint8_t FALLBACK_STATIC_IP[4] = {192, 168, 76, 2};
static const uint8_t FALLBACK_STATIC_MASK[4] = {255, 255, 255, 0};
static const uint8_t FALLBACK_STATIC_GW[4] = {192, 168, 76, 1};
static const uint8_t FALLBACK_STATIC_DNS[4] = {1, 1, 1, 1};

typedef struct {
    const uint8_t* data;
    size_t size;
} boot_blob_t;

typedef struct {
    bool safe_mode;
    bool no_framebuffer;
    bool no_usb;
    bool no_network;
    bool no_ata;
    bool serial_first;
} boot_options_t;

static char upper_ascii(char c) {
    if (c >= 'a' && c <= 'z') {
        return (char)(c - ('a' - 'A'));
    }
    return c;
}

static bool token_equals_nocase(const char* token, size_t token_len, const char* literal) {
    if (!token || !literal) return false;
    size_t literal_len = strlen(literal);
    if (token_len != literal_len) return false;

    for (size_t i = 0; i < literal_len; i++) {
        if (upper_ascii(token[i]) != upper_ascii(literal[i])) {
            return false;
        }
    }
    return true;
}

static bool cmdline_has_flag(const char* cmdline, const char* flag) {
    if (!cmdline || !flag || flag[0] == '\0') return false;

    size_t i = 0;
    while (cmdline[i] != '\0') {
        while (cmdline[i] == ' ' || cmdline[i] == '\t' || cmdline[i] == '\n' || cmdline[i] == ',') {
            i++;
        }
        if (cmdline[i] == '\0') break;

        size_t start = i;
        while (cmdline[i] != '\0' &&
               cmdline[i] != ' ' &&
               cmdline[i] != '\t' &&
               cmdline[i] != '\n' &&
               cmdline[i] != ',') {
            i++;
        }

        if (token_equals_nocase(&cmdline[start], i - start, flag)) {
            return true;
        }
    }

    return false;
}

static boot_options_t boot_options_from_cmdline(const char* cmdline) {
    boot_options_t opts = {0};

    opts.safe_mode = cmdline_has_flag(cmdline, "safe");
    opts.no_framebuffer = opts.safe_mode ||
                          cmdline_has_flag(cmdline, "nomodeset") ||
                          cmdline_has_flag(cmdline, "nofb");
    opts.no_usb = opts.safe_mode || cmdline_has_flag(cmdline, "nousb");
    opts.no_network = opts.safe_mode || cmdline_has_flag(cmdline, "nonet");
    opts.no_ata = opts.safe_mode || cmdline_has_flag(cmdline, "noata");
    opts.serial_first = opts.safe_mode || cmdline_has_flag(cmdline, "serialfirst");

    return opts;
}

static void log_boot_options(const boot_options_t* opts, const char* cmdline) {
    if (!opts) return;

    serial_write("[BOOT] Kernel cmdline: ");
    if (cmdline && cmdline[0] != '\0') {
        serial_write(cmdline);
    } else {
        serial_write("(empty)");
    }
    serial_write("\n");

    if (!opts->safe_mode && !opts->no_framebuffer && !opts->no_usb &&
        !opts->no_network && !opts->no_ata && !opts->serial_first) {
        serial_write("[BOOT] Profile: default\n");
        return;
    }

    serial_write("[BOOT] Profile: ");
    if (opts->safe_mode) serial_write("safe ");
    if (opts->no_framebuffer) serial_write("nofb ");
    if (opts->no_usb) serial_write("nousb ");
    if (opts->no_network) serial_write("nonet ");
    if (opts->no_ata) serial_write("noata ");
    if (opts->serial_first) serial_write("serialfirst ");
    serial_write("\n");
}

static __attribute__((noreturn)) void kernel_halt_error(const char* message) {
    if (message) {
        serial_write("[KERNEL] ERROR: ");
        serial_write(message);
        serial_write("\n");

        static const char prefix[] = "\n[KERNEL] ERROR: ";
        terminal_write(prefix, sizeof(prefix) - 1);
        terminal_write(message, strlen(message));
        terminal_write("\n", 1);
    }

    panic_halt_message(message ? message : "Kernel halt");
}

static bool multiboot_module_to_blob(const struct multiboot_tag_module* module, boot_blob_t* out) {
    if (!module || !out) return false;
    if (module->mod_end <= module->mod_start) return false;

    out->data = (const uint8_t*)PHYS_TO_VIRT((uint64_t)module->mod_start);
    out->size = (size_t)(module->mod_end - module->mod_start);
    return out->data != NULL && out->size > 0;
}

static bool blob_is_elf(const boot_blob_t* blob) {
    if (!blob || !blob->data || blob->size < 4) return false;
    return blob->data[0] == 0x7F && blob->data[1] == 'E' &&
           blob->data[2] == 'L' && blob->data[3] == 'F';
}

static bool blob_is_psf(const boot_blob_t* blob) {
    if (!blob || !blob->data || blob->size < 2) return false;
    return blob->data[0] == PSF1_MAGIC0 && blob->data[1] == PSF1_MAGIC1;
}

static const struct multiboot_tag_module* find_module_by_magic(bool want_elf) {
    size_t count = multiboot_get_module_count();
    for (size_t i = 0; i < count; i++) {
        const struct multiboot_tag_module* module = multiboot_get_module(i);
        boot_blob_t blob = {0};
        if (!multiboot_module_to_blob(module, &blob)) continue;
        if (want_elf ? blob_is_elf(&blob) : blob_is_psf(&blob)) {
            return module;
        }
    }
    return NULL;
}

static bool mount_boot_filesystem(void) {
    if (!fat16_mount(0)) {
        serial_write("[KERNEL] WARNING: Failed to mount FAT16 filesystem on ATA LBA0\n");
        return false;
    }

    vfs_filesystem_t* fat = fat16_create();
    if (!fat) {
        serial_write("[KERNEL] WARNING: Failed to create FAT16 VFS\n");
        return false;
    }

    if (!vfs_mount(fat, "/")) {
        serial_write("[KERNEL] WARNING: Failed to mount FAT16 VFS at /\n");
        return false;
    }

    return true;
}

static void bootstrap_filesystem_layout(void) {
    static const char* persistent_dirs[] = {
        "/etc",
        "/home",
        "/var",
        "/boot"
    };

    for (size_t i = 0; i < (sizeof(persistent_dirs) / sizeof(persistent_dirs[0])); i++) {
        if (!vfs_ensure_directory(persistent_dirs[i])) {
            serial_write("[KERNEL] WARNING: Failed to ensure directory ");
            serial_write(persistent_dirs[i]);
            serial_write("\n");
        }
    }

    vfs_filesystem_t* tmpfs = tmpfs_create();
    if (!tmpfs) {
        serial_write("[KERNEL] WARNING: Failed to create tmpfs for /tmp\n");
        return;
    }

    if (!vfs_mount_at(tmpfs, "/tmp")) {
        serial_write("[KERNEL] WARNING: Failed to mount tmpfs at /tmp\n");
    }

    vfs_filesystem_t* logfs = tmpfs_create();
    if (!logfs) {
        serial_write("[KERNEL] WARNING: Failed to create tmpfs for /var/log\n");
        return;
    }
    if (!vfs_mount_at(logfs, "/var/log")) {
        serial_write("[KERNEL] WARNING: Failed to mount tmpfs at /var/log\n");
    }
}

void kernel_main(uint64_t multiboot_addr) {
    serial_init();
    serial_write("[KERNEL] Starting SzczupakOS...\n");
    vga_init();
    terminal_init();

    static const char boot_banner[] = "SzczupakOS: booting kernel...\n";
    terminal_write(boot_banner, sizeof(boot_banner) - 1);
    
    if (!multiboot_parse(multiboot_addr)) {
        kernel_halt_error("Failed to parse multiboot info");
    }

    const char* boot_cmdline = multiboot_get_cmdline();
    boot_options_t boot_opts = boot_options_from_cmdline(boot_cmdline);
    log_boot_options(&boot_opts, boot_cmdline);
    terminal_set_serial_preferred(boot_opts.serial_first);
    
    if (!heap_init()) {
        kernel_halt_error("Heap initialization failed");
    }
    
    if (!vmm_init()) {
        kernel_halt_error("Virtual memory initialization failed");
    }
    
    if (!boot_opts.no_framebuffer) {
        struct multiboot_tag_framebuffer* fb_tag = multiboot_get_framebuffer_tag();
        if (fb_tag) {
            if (!framebuffer_init(fb_tag)) {
                serial_write("[KERNEL] WARNING: Framebuffer initialization failed\n");
            }
        } else {
            serial_write("[KERNEL] WARNING: No framebuffer tag from bootloader (falling back to VGA text)\n");
        }
    } else {
        serial_write("[BOOT] Framebuffer disabled by boot option\n");
    }
    
    if (!pagefault_init()) {
        kernel_halt_error("Page fault handler initialization failed");
    }
    
    arch_init_early();
    
    if (!syscall_init()) {
        kernel_halt_error("Syscall initialization failed");
    }

    uaccess_init();
    (void)acpi_init();
    driver_model_init();
    driver_register_builtin_descriptors();
    
    arch_init_timer(100);
    keyboard_init();
    mouse_init();
    rtc_init();
    (void)apic_init();
    pci_init();
    (void)gpu_init();
    if (gpu_available() && framebuffer_available()) {
        gpu_info_t gpu;
        framebuffer_info_t* fb = framebuffer_get_info();
        if (fb && gpu_get_info(&gpu)) {
            uint64_t fb_phys_page = fb->address & ~0xFFFULL;
            uint64_t gpu_mmio_page = gpu.mmio_base & ~0xFFFULL;
            if (gpu_mmio_page != 0u && fb_phys_page == gpu_mmio_page) {
                serial_write("[GPU] Boot framebuffer is mapped from primary GPU BAR\n");
            } else {
                serial_write("[GPU] Boot framebuffer base differs from primary GPU MMIO BAR\n");
            }
        }
    }
    driver_probe_all();
    bool net_ready = false;
    serial_write("[HW] Adaptive hardware mode: probing host devices and selecting drivers\n");

    if (!boot_opts.no_usb) {
        usb_init();
    } else {
        serial_write("[BOOT] USB init disabled by boot option\n");
    }

    bool host_rtl8168_ready = false;
    if (!boot_opts.no_network) {
        host_rtl8168_ready = rtl8168_init();
        if (host_rtl8168_ready) {
            serial_write("[HW] Host NIC profile detected: Realtek RTL8168 family\n");
        }

        if (net_init()) {
            arch_irq_enable();
            if (!net_configure_dhcp(8000)) {
                serial_write("[KERNEL] WARNING: DHCP configuration failed, switching to static 192.168.76.2\n");
                if (!net_configure_static(FALLBACK_STATIC_IP, FALLBACK_STATIC_MASK,
                                          FALLBACK_STATIC_GW, FALLBACK_STATIC_DNS)) {
                    serial_write("[KERNEL] WARNING: Static network fallback failed\n");
                }
            }
            arch_irq_disable();
            net_ready = net_is_ready();
            if (net_ready) {
                serial_write("[HW] Network backend selected: ");
                serial_write(net_get_backend_name());
                serial_write("\n");
            }
        } else {
            if (host_rtl8168_ready) {
                serial_write("[HW] Native RTL8168 detected; full stack integration is in progress\n");
            }
            serial_write("[KERNEL] WARNING: Network initialization failed\n");
        }
    } else {
        serial_write("[BOOT] Network stack disabled by boot option\n");
    }
    
    if (!task_init()) {
        kernel_halt_error("Task initialization failed");
    }
    pty_init();
    
    scheduler_init();
    vfs_init();
    if (!boot_opts.no_ata) {
        ata_init();
    } else {
        serial_write("[BOOT] ATA init disabled by boot option\n");
    }

    if (framebuffer_available()) {
        fb_color_t bg = {0, 0, 0, 0};
        fb_clear(bg);
    }

    const struct multiboot_tag_module* shell_module = multiboot_find_module("SHELL.ELF");
    const struct multiboot_tag_module* font_module = multiboot_find_module("FONT.PSF");
    if (!shell_module) shell_module = find_module_by_magic(true);
    if (!font_module) font_module = find_module_by_magic(false);

    boot_blob_t shell_blob = {0};
    boot_blob_t font_blob = {0};
    bool shell_from_module = multiboot_module_to_blob(shell_module, &shell_blob) && blob_is_elf(&shell_blob);
    bool font_from_module = multiboot_module_to_blob(font_module, &font_blob) && blob_is_psf(&font_blob);

    bool fs_ready = false;
    if (!boot_opts.no_ata) {
        fs_ready = mount_boot_filesystem();
        if (fs_ready) {
            bootstrap_filesystem_layout();
        }
    }

    if (font_from_module) {
        if (!psf_load_from_memory(font_blob.data, font_blob.size)) {
            serial_write("[KERNEL] WARNING: Failed to load font from multiboot module\n");
        }
    } else if (fs_ready) {
        if (!psf_load("/FONT.PSF")) {
            serial_write("[KERNEL] WARNING: Failed to load font from FAT16\n");
        }
    }

    uint8_t* shell_data = NULL;
    size_t shell_size = 0;
    bool shell_heap_owned = false;
    const char* shell_cmdline = "/SHELL.ELF";

    if (shell_from_module) {
        shell_data = (uint8_t*)shell_blob.data;
        shell_size = shell_blob.size;
        shell_cmdline = "/BOOT/SHELL.ELF";
        serial_write("[KERNEL] Using shell from multiboot module\n");
    } else if (fs_ready) {
        vfs_node_t* shell_file = vfs_open("/SHELL.ELF", 0);
        if (!shell_file) {
            kernel_halt_error("Failed to open shell executable");
        }

        if (shell_file->size == 0) {
            vfs_close(shell_file);
            kernel_halt_error("Shell executable has zero size");
        }

        shell_data = kmalloc(shell_file->size);
        if (!shell_data) {
            vfs_close(shell_file);
            kernel_halt_error("Failed to allocate memory for shell");
        }

        if (!vfs_read(shell_file, shell_data, 0, shell_file->size)) {
            kfree(shell_data);
            vfs_close(shell_file);
            kernel_halt_error("Failed to read shell executable");
        }

        shell_size = shell_file->size;
        shell_heap_owned = true;
        vfs_close(shell_file);
    } else {
        kernel_halt_error("No boot shell available (missing multiboot module and FAT16)");
    }

    if (!shell_data || shell_size == 0) {
        kernel_halt_error("Shell image is empty");
    }

    task_t* task = task_create_user("shell", shell_cmdline, shell_data, shell_size);
    if (!task) {
        if (shell_heap_owned) kfree(shell_data);
        kernel_halt_error("Failed to create shell task");
    }

    if (shell_heap_owned) {
        kfree(shell_data);
    }

    if (!task->kernel_stack) {
        kernel_halt_error("Shell task has no kernel stack");
    }

    arch_set_kernel_stack((uint64_t)task->kernel_stack + KERNEL_STACK_SIZE);

    scheduler_enable();
    arch_irq_enable();

    serial_write("[KERNEL] System ready, entering idle loop\n");
    
    while (1) {
        if (net_ready) {
            net_poll();
        }
        arch_wait_for_interrupt();
    }
}
