MAGIC    equ 0xe85250d6
ARCH     equ 0
HEADER_LEN equ header_end - header_start
CHECKSUM equ -(MAGIC + ARCH + HEADER_LEN)

section .multiboot
align 8
header_start:
    dd MAGIC
    dd ARCH
    dd HEADER_LEN
    dd CHECKSUM
    
    align 8
framebuffer_tag_start:
    dw 5
    dw 0
    dd framebuffer_tag_end - framebuffer_tag_start
    dd 1024
    dd 768
    dd 32
framebuffer_tag_end:
    
    align 8
    dw 0
    dw 0
    dd 8
header_end:

section .bss
align 16
stack_bottom:
    resb 16384
stack_top:

align 4096
global p4_table
global p3_table
global p2_table
global p3_table_higher
global p2_tables_higher
p4_table:
    resb 4096
p3_table:
    resb 4096
p2_table:
    resb 4096
p3_table_higher:
    resb 4096
p2_tables_higher:
    resb 4096 * 4

section .text
bits 32
global _start

_start:
    mov esp, stack_top
    
    push ebx
    
    cmp eax, 0x36d76289
    jne .no_multiboot
    
    call check_cpuid
    call check_long_mode
    call setup_page_tables
    call enable_paging
    
    lgdt [gdt64.pointer]
    
    pop ebx
    
    jmp gdt64.code:long_mode_start

.no_multiboot:
    mov dword [0xb8000], 0x4f524f45
    hlt

check_cpuid:
    pushfd
    pop eax
    mov ecx, eax
    xor eax, 1 << 21
    push eax
    popfd
    pushfd
    pop eax
    push ecx
    popfd
    xor eax, ecx
    jz .no_cpuid
    ret
.no_cpuid:
    mov dword [0xb8000], 0x4f214f21
    hlt

check_long_mode:
    mov eax, 0x80000000
    cpuid
    cmp eax, 0x80000001
    jb .no_long_mode
    
    mov eax, 0x80000001
    cpuid
    test edx, 1 << 29
    jz .no_long_mode
    ret
.no_long_mode:
    mov dword [0xb8000], 0x4f3f4f3f
    hlt

setup_page_tables:
    mov eax, p3_table
    or eax, 0b11
    mov [p4_table], eax
    
    mov eax, p3_table_higher
    or eax, 0b11
    mov [p4_table + 256 * 8], eax
    
    mov eax, p2_table
    or eax, 0b11
    mov [p3_table], eax
    
    mov ecx, 0
.map_p2_identity:
    mov eax, 0x200000
    mov edx, ecx
    mul edx
    or eax, 0b10000011
    mov [p2_table + ecx * 8], eax
    inc ecx
    cmp ecx, 512
    jne .map_p2_identity
    
    mov eax, p2_tables_higher
    or eax, 0b11
    mov [p3_table_higher], eax
    
    mov eax, p2_tables_higher + 4096
    or eax, 0b11
    mov [p3_table_higher + 8], eax
    
    mov eax, p2_tables_higher + 8192
    or eax, 0b11
    mov [p3_table_higher + 16], eax
    
    mov eax, p2_tables_higher + 12288
    or eax, 0b11
    mov [p3_table_higher + 24], eax
    
    mov ecx, 0
.map_p2_higher:
    mov eax, 0x200000
    mov edx, ecx
    mul edx
    or eax, 0b10000011
    mov [p2_tables_higher + ecx * 8], eax
    inc ecx
    cmp ecx, 2048
    jne .map_p2_higher
    
    ret

enable_paging:
    mov eax, p4_table
    mov cr3, eax
    
    mov eax, cr4
    or eax, 1 << 5
    mov cr4, eax
    
    mov ecx, 0xC0000080
    rdmsr
    or eax, 1 << 8
    wrmsr
    
    mov eax, cr0
    or eax, 1 << 31
    or eax, 1 << 16
    mov cr0, eax
    ret

section .rodata
gdt64:
    dq 0
.code: equ $ - gdt64
    dq (1<<43) | (1<<44) | (1<<47) | (1<<53)
.data: equ $ - gdt64
    dq (1<<44) | (1<<47) | (1<<41)
.pointer:
    dw $ - gdt64 - 1
    dq gdt64

section .text
bits 64

long_mode_start:
    xor ax, ax
    mov ss, ax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    
    mov rsp, stack_top
    
    mov edi, ebx
    
    extern kernel_main
    call kernel_main
    
    cli
.hang:
    hlt
    jmp .hang
