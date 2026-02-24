bits 64
section .text

global _start

_start:
    xor rbp, rbp
    
    extern main
    call main
    
    mov rdi, rax
    mov rax, 0
    syscall
    
.hang:
    hlt
    jmp .hang
