#include <kernel/elf.h>
#include <kernel/vmm.h>
#include <kernel/pmm.h>
#include <kernel/task.h>
#include <kernel/vga.h>
#include <kernel/serial.h>

static void memcpy(void* dest, void* src, size_t n) {
    char* d = (char*)dest;
    char* s = (char*)src;
    while (n--) *d++ = *s++;
}

static void memset(void* dest, int c, size_t n) {
    char* d = (char*)dest;
    while (n--) *d++ = (char)c;
}

#define ALIGN_UP(addr, align) (((addr) + (align) - 1) & ~((align) - 1))

bool elf_validate(Elf64_Ehdr* header) {
    if (header->e_ident[0] != ELFMAG0 ||
        header->e_ident[1] != ELFMAG1 ||
        header->e_ident[2] != ELFMAG2 ||
        header->e_ident[3] != ELFMAG3) {
        vga_write("[ELF] Bad magic\n");
        return false;
    }
    
    if (header->e_ident[4] != ELFCLASS64) {
        vga_write("[ELF] Not 64-bit\n");
        return false;
    }
    
    if (header->e_ident[5] != ELFDATA2LSB) {
        vga_write("[ELF] Not little endian\n");
        return false;
    }
    
    if (header->e_ident[6] != EV_CURRENT) {
        vga_write("[ELF] Wrong version\n");
        return false;
    }
    
    if (header->e_type != ET_EXEC) {
        vga_write("[ELF] Not executable\n");
        return false;
    }
    
    if (header->e_machine != 0x3E) {
        vga_write("[ELF] Wrong arch\n");
        return false;
    }
    
    return true;
}

uint64_t elf_load(task_t* task, uint8_t* buffer, size_t size) {
    Elf64_Ehdr* header = (Elf64_Ehdr*)buffer;
    
    if (!elf_validate(header)) {
        return 0;
    }
    
    serial_write("[ELF] Loading...\n");
    serial_write("[ELF] Entry point from header: 0x");
    serial_write_hex(header->e_entry);
    serial_write("\n");
    serial_write("[ELF] Program headers: ");
    serial_write_dec(header->e_phnum);
    serial_write("\n");
    
    Elf64_Phdr* phdr = (Elf64_Phdr*)(buffer + header->e_phoff);
    
    for (int i = 0; i < header->e_phnum; i++) {
        if (phdr[i].p_type == PT_LOAD) {
            serial_write("[ELF] Segment ");
            serial_write_dec(i);
            serial_write(": vaddr=0x");
            serial_write_hex(phdr[i].p_vaddr);
            serial_write(" memsz=");
            serial_write_dec(phdr[i].p_memsz);
            serial_write(" filesz=");
            serial_write_dec(phdr[i].p_filesz);
            serial_write(" offset=0x");
            serial_write_hex(phdr[i].p_offset);
            serial_write("\n");
            
            uint64_t vaddr_start = phdr[i].p_vaddr;
            uint64_t vaddr_end = ALIGN_UP(vaddr_start + phdr[i].p_memsz, 4096);
            uint64_t file_end = phdr[i].p_offset + phdr[i].p_filesz;
            
            for (uint64_t vaddr = vaddr_start; vaddr < vaddr_end; vaddr += 4096) {
                uint64_t phys = pmm_alloc_page();
                if (!phys) {
                    serial_write("[ELF] No memory\n");
                    return 0;
                }
                
                uint8_t* page = (uint8_t*)phys;
                memset(page, 0, 4096);
                
                if (vaddr >= vaddr_start && vaddr < vaddr_start + phdr[i].p_filesz) {
                    uint64_t offset_in_segment = vaddr - vaddr_start;
                    uint64_t file_offset = phdr[i].p_offset + offset_in_segment;
                    
                    if (file_offset < file_end) {
                        uint64_t bytes_to_copy = 4096;
                        if (file_offset + bytes_to_copy > file_end) {
                            bytes_to_copy = file_end - file_offset;
                        }
                        
                        serial_write("[ELF] Copying ");
                        serial_write_dec(bytes_to_copy);
                        serial_write(" bytes from file 0x");
                        serial_write_hex(file_offset);
                        serial_write(" to phys 0x");
                        serial_write_hex(phys);
                        serial_write("\n");
                        
                        serial_write("[ELF] Source bytes: ");
                        for(int k = 0; k < 8 && k < bytes_to_copy; k++) {
                            serial_write_hex(buffer[file_offset + k]);
                            serial_write(" ");
                        }
                        serial_write("\n");
                        
                        memcpy(page, buffer + file_offset, bytes_to_copy);
                        
                        serial_write("[ELF] Dest bytes: ");
                        for(int k = 0; k < 8; k++) {
                            serial_write_hex(page[k]);
                            serial_write(" ");
                        }
                        serial_write("\n");
                    }
                }
                
                uint32_t flags = PAGE_PRESENT | PAGE_USER;
                if (phdr[i].p_flags & PF_W) {
                    flags |= PAGE_WRITE;
                }

                if (!vmm_map_user_page(task->page_dir, vaddr, phys, flags)) {
                    serial_write("[ELF] Failed to map page\n");
                    return 0;
                }
            }
        }
    }
    
    serial_write("[ELF] Verifying entry point\n");
    uint64_t entry_phys = vmm_get_physical(task->page_dir, header->e_entry);
    serial_write("[ELF] Entry phys: 0x");
    serial_write_hex(entry_phys);
    serial_write("\n");
    
    if (entry_phys) {
        uint8_t* code = (uint8_t*)entry_phys;
        serial_write("[ELF] Code at entry: ");
        for (int j = 0; j < 16; j++) {
            serial_write_hex(code[j]);
            serial_write(" ");
        }
        serial_write("\n");
    }
    
    return header->e_entry;
}