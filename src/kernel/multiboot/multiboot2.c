#include <drivers/serial.h>
#include <kernel/vga.h>
#include <mm/pmm.h>
#include <kernel/multiboot2.h>
#include <kernel/string.h>

extern uint8_t kernel_end;

static struct multiboot_tag_framebuffer* saved_fb_tag = NULL;
static const struct multiboot_tag_module* saved_modules[32];
static size_t saved_module_count = 0;
static char saved_cmdline[256];
static uint8_t saved_acpi_old_rsdp[20];
static size_t saved_acpi_old_size = 0;
static uint8_t saved_acpi_new_rsdp[64];
static size_t saved_acpi_new_size = 0;

static char upper_ascii(char c) {
    if (c >= 'a' && c <= 'z') {
        return (char)(c - ('a' - 'A'));
    }
    return c;
}

static bool contains_token_nocase(const char* haystack, const char* needle) {
    if (!haystack || !needle) return false;

    size_t needle_len = strlen(needle);
    if (needle_len == 0) return false;

    size_t hay_len = strlen(haystack);
    if (hay_len < needle_len) return false;

    for (size_t i = 0; i + needle_len <= hay_len; i++) {
        bool match = true;
        for (size_t j = 0; j < needle_len; j++) {
            if (upper_ascii(haystack[i + j]) != upper_ascii(needle[j])) {
                match = false;
                break;
            }
        }
        if (match) return true;
    }

    return false;
}

bool multiboot_parse(uint64_t multiboot_addr) {
    if (multiboot_addr == 0) {
        serial_write("[MULTIBOOT] ERROR: Invalid multiboot address\n");
        return false;
    }

    uint32_t mb_total_size = *((uint32_t*)multiboot_addr);
    if (mb_total_size < 8) {
        serial_write("[MULTIBOOT] ERROR: Invalid multiboot total size\n");
        return false;
    }
    
    struct multiboot_tag* tag;
    struct multiboot_tag_mmap* mmap_tag = NULL;
    uint64_t highest_addr = 0;

    saved_fb_tag = NULL;
    saved_module_count = 0;
    saved_cmdline[0] = '\0';
    saved_acpi_old_size = 0;
    saved_acpi_new_size = 0;
    
    for (tag = (struct multiboot_tag*)(multiboot_addr + 8);
         tag->type != MULTIBOOT_TAG_TYPE_END;
         tag = (struct multiboot_tag*)((uint64_t)tag + ((tag->size + 7) & ~7))) {
        
        if (tag->type == MULTIBOOT_TAG_TYPE_MMAP) {
            struct multiboot_tag_mmap* mmap = (struct multiboot_tag_mmap*)tag;
            mmap_tag = mmap;
            struct multiboot_mmap_entry* entry;
            
            for (entry = (struct multiboot_mmap_entry*)((uint64_t)mmap + 16);
                 (uint64_t)entry < (uint64_t)mmap + mmap->size;
                 entry = (struct multiboot_mmap_entry*)((uint64_t)entry + mmap->entry_size)) {
                
                if (entry->type == MULTIBOOT_MEMORY_AVAILABLE) {
                    uint64_t end_addr = entry->addr + entry->len;
                    if (end_addr > highest_addr) {
                        highest_addr = end_addr;
                    }
                }
            }
        }
        
        if (tag->type == MULTIBOOT_TAG_TYPE_FRAMEBUFFER) {
            saved_fb_tag = (struct multiboot_tag_framebuffer*)tag;
        }

        if (tag->type == MULTIBOOT_TAG_TYPE_CMDLINE && tag->size > sizeof(struct multiboot_tag)) {
            const char* cmdline = (const char*)((const uint8_t*)tag + sizeof(struct multiboot_tag));
            size_t payload = (size_t)tag->size - sizeof(struct multiboot_tag);
            size_t copy_len = payload;
            if (copy_len >= sizeof(saved_cmdline)) {
                copy_len = sizeof(saved_cmdline) - 1;
            }
            memcpy(saved_cmdline, cmdline, copy_len);
            saved_cmdline[copy_len] = '\0';

            for (size_t i = 0; i < copy_len; i++) {
                if (saved_cmdline[i] == '\0') {
                    break;
                }
                if (i + 1 == copy_len) {
                    saved_cmdline[copy_len] = '\0';
                }
            }
        }

        if (tag->type == MULTIBOOT_TAG_TYPE_MODULE && saved_module_count < (sizeof(saved_modules) / sizeof(saved_modules[0]))) {
            struct multiboot_tag_module* module = (struct multiboot_tag_module*)tag;
            if (module->mod_end > module->mod_start) {
                saved_modules[saved_module_count++] = module;
            }
        }

        if (tag->type == MULTIBOOT_TAG_TYPE_ACPI_OLD && tag->size > sizeof(struct multiboot_tag)) {
            const uint8_t* rsdp = (const uint8_t*)tag + sizeof(struct multiboot_tag);
            size_t payload = (size_t)tag->size - sizeof(struct multiboot_tag);
            if (payload > sizeof(saved_acpi_old_rsdp)) {
                payload = sizeof(saved_acpi_old_rsdp);
            }
            memcpy(saved_acpi_old_rsdp, rsdp, payload);
            saved_acpi_old_size = payload;
        }

        if (tag->type == MULTIBOOT_TAG_TYPE_ACPI_NEW && tag->size > sizeof(struct multiboot_tag)) {
            const uint8_t* rsdp = (const uint8_t*)tag + sizeof(struct multiboot_tag);
            size_t payload = (size_t)tag->size - sizeof(struct multiboot_tag);
            if (payload > sizeof(saved_acpi_new_rsdp)) {
                payload = sizeof(saved_acpi_new_rsdp);
            }
            memcpy(saved_acpi_new_rsdp, rsdp, payload);
            saved_acpi_new_size = payload;
        }
    }
    
    pmm_init(0, highest_addr);
    pmm_reserve_range(multiboot_addr, multiboot_addr + mb_total_size);
    if (mmap_tag) {
        struct multiboot_mmap_entry* entry;
        for (entry = (struct multiboot_mmap_entry*)((uint64_t)mmap_tag + 16);
             (uint64_t)entry < (uint64_t)mmap_tag + mmap_tag->size;
             entry = (struct multiboot_mmap_entry*)((uint64_t)entry + mmap_tag->entry_size)) {
            if (entry->type != MULTIBOOT_MEMORY_AVAILABLE) {
                pmm_reserve_range(entry->addr, entry->addr + entry->len);
            }
        }
    }

    for (size_t i = 0; i < saved_module_count; i++) {
        const struct multiboot_tag_module* module = saved_modules[i];
        pmm_reserve_range(module->mod_start, module->mod_end);
    }

    serial_write("[MULTIBOOT] Modules: ");
    serial_write_dec((uint32_t)saved_module_count);
    serial_write("\n");
    if (saved_cmdline[0] != '\0') {
        serial_write("[MULTIBOOT] Kernel cmdline: ");
        serial_write(saved_cmdline);
        serial_write("\n");
    }

    serial_write("[MULTIBOOT] Multiboot info parsed successfully\n");
    return true;
}

struct multiboot_tag_framebuffer* multiboot_get_framebuffer_tag(void) {
    return saved_fb_tag;
}

const char* multiboot_get_cmdline(void) {
    return saved_cmdline;
}

size_t multiboot_get_module_count(void) {
    return saved_module_count;
}

const struct multiboot_tag_module* multiboot_get_module(size_t index) {
    if (index >= saved_module_count) {
        return NULL;
    }
    return saved_modules[index];
}

const struct multiboot_tag_module* multiboot_find_module(const char* token) {
    if (!token || token[0] == '\0') {
        return NULL;
    }

    for (size_t i = 0; i < saved_module_count; i++) {
        const struct multiboot_tag_module* module = saved_modules[i];
        if (contains_token_nocase(module->cmdline, token)) {
            return module;
        }
    }

    return NULL;
}

const void* multiboot_get_acpi_rsdp_old(size_t* out_size) {
    if (out_size) {
        *out_size = saved_acpi_old_size;
    }
    return (saved_acpi_old_size > 0) ? (const void*)saved_acpi_old_rsdp : NULL;
}

const void* multiboot_get_acpi_rsdp_new(size_t* out_size) {
    if (out_size) {
        *out_size = saved_acpi_new_size;
    }
    return (saved_acpi_new_size > 0) ? (const void*)saved_acpi_new_rsdp : NULL;
}
