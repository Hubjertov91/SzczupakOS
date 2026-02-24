#include <kernel/elf.h>
#include <mm/vmm.h>
#include <mm/pmm.h>
#include <drivers/serial.h>
#include <task/task.h> 

#define PHYS_TO_VIRT(phys) ((void*)((uint64_t)(phys) + 0xFFFF800000000000ULL))

typedef struct {
    uint32_t magic;
    uint8_t elf_class;
    uint8_t endianness;
    uint8_t version;
    uint8_t abi;
    uint64_t unused;
    uint16_t type;
    uint16_t machine;
    uint32_t version2;
    uint64_t entry;
    uint64_t phoff;
    uint64_t shoff;
    uint32_t flags;
    uint16_t ehsize;
    uint16_t phentsize;
    uint16_t phnum;
    uint16_t shentsize;
    uint16_t shnum;
    uint16_t shstrndx;
} __attribute__((packed)) elf64_header_t;

typedef struct {
    uint32_t type;
    uint32_t flags;
    uint64_t offset;
    uint64_t vaddr;
    uint64_t paddr;
    uint64_t filesz;
    uint64_t memsz;
    uint64_t align;
} __attribute__((packed)) elf64_program_header_t;

static bool elf_copy_to_user(page_directory_t* dir, uint64_t dst_vaddr, const uint8_t* src, uint64_t len) {
    if (!dir || !src || len == 0) return false;

    uint64_t copied = 0;
    while (copied < len) {
        uint64_t vaddr = dst_vaddr + copied;
        uint64_t phys = vmm_get_physical(dir, vaddr);
        if (!phys) return false;

        uint64_t page_off = vaddr & 0xFFFULL;
        uint64_t chunk = 4096ULL - page_off;
        if (chunk > (len - copied)) chunk = len - copied;

        uint8_t* dst = (uint8_t*)PHYS_TO_VIRT(phys & ~0xFFFULL) + page_off;
        for (uint64_t i = 0; i < chunk; i++) {
            dst[i] = src[copied + i];
        }

        copied += chunk;
    }

    return true;
}

static bool elf_zero_user(page_directory_t* dir, uint64_t dst_vaddr, uint64_t len) {
    if (!dir || len == 0) return false;

    uint64_t zeroed = 0;
    while (zeroed < len) {
        uint64_t vaddr = dst_vaddr + zeroed;
        uint64_t phys = vmm_get_physical(dir, vaddr);
        if (!phys) return false;

        uint64_t page_off = vaddr & 0xFFFULL;
        uint64_t chunk = 4096ULL - page_off;
        if (chunk > (len - zeroed)) chunk = len - zeroed;

        uint8_t* dst = (uint8_t*)PHYS_TO_VIRT(phys & ~0xFFFULL) + page_off;
        for (uint64_t i = 0; i < chunk; i++) {
            dst[i] = 0;
        }

        zeroed += chunk;
    }

    return true;
}

uint64_t elf_load(task_t* task, uint8_t* elf_data, size_t elf_size) {
    if (!task || !task->page_dir || !elf_data || elf_size < sizeof(elf64_header_t)) {
        serial_write("[ELF] Invalid load parameters\n");
        return 0;
    }

    elf64_header_t* header = (elf64_header_t*)elf_data;

    if (header->magic != 0x464C457F) {
        serial_write("[ELF] Invalid magic\n");
        return 0;
    }

    uint64_t ph_end = header->phoff + ((uint64_t)header->phnum * header->phentsize);
    if (header->phoff >= elf_size || ph_end > elf_size || header->phentsize < sizeof(elf64_program_header_t)) {
        serial_write("[ELF] Invalid program header table\n");
        return 0;
    }
    
    serial_write("[ELF] Loading ELF, entry=0x");
    serial_write_hex(header->entry);
    serial_write(" phnum=");
    serial_write_dec(header->phnum);
    serial_write("\n");

    for (uint16_t i = 0; i < header->phnum; i++) {
        elf64_program_header_t* ph = (elf64_program_header_t*)(elf_data + header->phoff + i * header->phentsize);
        if (ph->type != 1) continue;
        if (ph->memsz == 0) continue;
        if (ph->filesz > ph->memsz) {
            serial_write("[ELF] Invalid segment sizes\n");
            return 0;
        }
        if (ph->offset + ph->filesz > elf_size) {
            serial_write("[ELF] Segment outside ELF image\n");
            return 0;
        }

        serial_write("[ELF] Segment ");
        serial_write_dec(i);
        serial_write(": vaddr=0x");
        serial_write_hex(ph->vaddr);
        serial_write(" memsz=0x");
        serial_write_hex(ph->memsz);
        serial_write(" filesz=0x");
        serial_write_hex(ph->filesz);
        serial_write(" flags=0x");
        serial_write_hex(ph->flags);
        serial_write("\n");

        uint64_t vstart = ph->vaddr & ~0xFFF;
        uint64_t vend   = (ph->vaddr + ph->memsz + 0xFFF) & ~0xFFF;

        uint32_t map_flags = PAGE_PRESENT | PAGE_USER | PAGE_WRITE;

        for (uint64_t v = vstart; v < vend; v += 4096) {
            uint64_t phys = vmm_get_physical(task->page_dir, v);
            if (!phys) {
                phys = pmm_alloc_page();
                if (!phys) {
                    serial_write("[ELF] Failed to allocate page\n");
                    return 0;
                }

                uint8_t* page = PHYS_TO_VIRT(phys);
                for (int j = 0; j < 4096; j++) page[j] = 0;
            }

            if (!vmm_map_user_page(task->page_dir, v, phys, map_flags)) {
                serial_write("[ELF] Failed to map page at 0x");
                serial_write_hex(v);
                serial_write("\n");
                return 0;
            }
        }

        if (ph->filesz > 0) {
            if (!elf_copy_to_user(task->page_dir, ph->vaddr, elf_data + ph->offset, ph->filesz)) {
                serial_write("[ELF] Cannot copy segment bytes\n");
                return 0;
            }
        }

        if (ph->memsz > ph->filesz) {
            if (!elf_zero_user(task->page_dir, ph->vaddr + ph->filesz, ph->memsz - ph->filesz)) {
                serial_write("[ELF] Cannot zero bss\n");
                return 0;
            }
        }

        if (!(ph->flags & 0x2)) {
            for (uint64_t v = vstart; v < vend; v += 4096) {
                if (!vmm_change_flags(task->page_dir, v, PAGE_PRESENT | PAGE_USER)) {
                    serial_write("[ELF] Failed to tighten RX perms at 0x");
                    serial_write_hex(v);
                    serial_write("\n");
                    return 0;
                }
            }
        }
    }

    serial_write("[ELF] Entry point: 0x");
    serial_write_hex(header->entry);
    serial_write("\n");

    return header->entry;
}
