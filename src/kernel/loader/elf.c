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

uint64_t elf_load(task_t* task, uint8_t* elf_data, size_t elf_size) {
    elf64_header_t* header = (elf64_header_t*)elf_data;
    
    (void)elf_size;
    
    if (header->magic != 0x464C457F) {
        serial_write("[ELF] Invalid magic\n");
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

        for (uint64_t v = vstart; v < vend; v += 4096) {
            uint64_t phys = pmm_alloc_page();
            if (!phys) {
                serial_write("[ELF] Failed to allocate page\n");
                return 0;
            }
            
            uint8_t* page = PHYS_TO_VIRT(phys);
            for (int j = 0; j < 4096; j++) page[j] = 0;

            uint32_t flags = PAGE_PRESENT | PAGE_USER;
            if (ph->flags & 0x2) flags |= PAGE_WRITE;
            
            if (!vmm_map_page(task->page_dir, v, phys, flags)) {
                serial_write("[ELF] Failed to map page at 0x");
                serial_write_hex(v);
                serial_write("\n");
                return 0;
            }
        }

        for (uint64_t off = 0; off < ph->filesz; off++) {
            uint64_t vaddr = ph->vaddr + off;
            uint64_t phys = vmm_get_physical(task->page_dir, vaddr);
            if (!phys) {
                serial_write("[ELF] Cannot get physical for vaddr 0x");
                serial_write_hex(vaddr);
                serial_write("\n");
                return 0;
            }
            
            *(uint8_t*)PHYS_TO_VIRT(phys) = elf_data[ph->offset + off];
        }
    }

    serial_write("[ELF] Entry point: 0x");
    serial_write_hex(header->entry);
    serial_write("\n");

    return header->entry;
}