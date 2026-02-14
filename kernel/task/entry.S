bits 64
section .text

global usermode_entry

extern serial_write

usermode_entry:
    push rdi
    mov rdi, before_iret_msg
    call serial_write
    pop rdi
    
    xor rax, rax
    xor rbx, rbx
    xor rcx, rcx
    xor rdx, rdx
    xor rsi, rsi
    xor rdi, rdi
    xor rbp, rbp
    xor r8, r8
    xor r9, r9
    xor r10, r10
    xor r11, r11
    xor r12, r12
    xor r13, r13
    xor r14, r14
    xor r15, r15
    
    iretq

section .rodata
before_iret_msg: db "[ENTRY] Before IRETQ", 10, 0