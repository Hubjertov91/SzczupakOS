bits 64
section .text

global switch_task

switch_task:
    test rdi, rdi
    jz .no_save
    
    push rbx
    push rbp
    push r12
    push r13
    push r14
    push r15
    mov [rdi + 48], rsp
    
.no_save:
    mov rsp, [rsi + 48]
    
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbp
    pop rbx
    
    ret