bits 64
section .text

extern main
global _start

_start:
    xor rbp, rbp
    
    call main
    
    mov rdi, rax
    int 0x80
    
.hang:
    hlt
    jmp .hang