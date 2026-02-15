#include <drivers/serial.h>
#include <kernel/vga.h>
#include <mm/pmm.h>
#include <kernel/multiboot2.h>

extern uint8_t kernel_end;

static struct multiboot_tag_framebuffer* saved_fb_tag = NULL;

void multiboot_parse(uint64_t multiboot_addr) {
    struct multiboot_tag* tag;
    uint64_t highest_addr = 0;
    
    for (tag = (struct multiboot_tag*)(multiboot_addr + 8);
         tag->type != MULTIBOOT_TAG_TYPE_END;
         tag = (struct multiboot_tag*)((uint64_t)tag + ((tag->size + 7) & ~7))) {
        
        if (tag->type == MULTIBOOT_TAG_TYPE_MMAP) {
            struct multiboot_tag_mmap* mmap = (struct multiboot_tag_mmap*)tag;
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
    }
    
    pmm_init(0, highest_addr);
}

struct multiboot_tag_framebuffer* multiboot_get_framebuffer_tag(void) {
    return saved_fb_tag;
}