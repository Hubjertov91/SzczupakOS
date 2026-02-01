bits 64
section .text

extern main
global _start

_start:
    xor rbp, rbp
    
    call main
    
    mov rdi, rax
    mov rax, 1
    syscall
    
.hang:
    hlt
    jmp .hang