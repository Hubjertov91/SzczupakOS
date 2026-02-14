bits 64
section .text

extern syscall_handler
global syscall_handler_asm
global syscall_kernel_rsp

section .bss
syscall_user_rsp:   resq 1
syscall_kernel_rsp: resq 1

section .text

syscall_handler_asm:
    mov [rel syscall_user_rsp], rsp
    mov rsp, [rel syscall_kernel_rsp]

    push qword [rel syscall_user_rsp]
    push r11
    push rcx

    push r15
    push r14
    push r13
    push r12
    push rbp
    push rbx
    push r10
    push r9
    push r8
    push rdi
    push rsi
    push rdx
    push rax

    sti

    mov rdi, rsp
    call syscall_handler

    cli

    pop rax
    pop rdx
    pop rsi
    pop rdi
    pop r8
    pop r9
    pop r10
    pop rbx
    pop rbp
    pop r12
    pop r13
    pop r14
    pop r15

    pop rcx
    pop r11
    pop rsp

    o64 sysret