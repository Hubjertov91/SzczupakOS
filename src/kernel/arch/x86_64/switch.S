bits 64
section .text

global switch_task

switch_task:
    test rdi, rdi
    jz .no_save

    mov [rdi + 0],  r15
    mov [rdi + 8],  r14
    mov [rdi + 16], r13
    mov [rdi + 24], r12
    mov [rdi + 32], rbp
    mov [rdi + 40], rbx
    mov [rdi + 48], rsp

.no_save:
    mov r15, [rsi + 0]
    mov r14, [rsi + 8]
    mov r13, [rsi + 16]
    mov r12, [rsi + 24]
    mov rbp, [rsi + 32]
    mov rbx, [rsi + 40]
    mov rsp, [rsi + 48]

    ret