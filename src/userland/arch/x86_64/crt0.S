bits 64
section .text

global _start

_start:
    xor rbp, rbp

    ; Initial user stack layout: [argc][argv0][argv1]...[NULL]
    mov rdi, [rsp]
    lea rsi, [rsp + 8]
    and rsp, -16
    
    extern main
    call main
    
    mov rdi, rax
    mov rax, 0
    syscall
    
.hang:
    hlt
    jmp .hang
