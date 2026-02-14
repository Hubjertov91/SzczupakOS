bits 64
section .text

global _start

_start:
    mov ax, 0x23
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    
    xor rbp, rbp
    
    extern main
    call main
    
    mov rdi, rax
    mov rax, 6
    syscall
    
.hang:
    hlt
    jmp .hang