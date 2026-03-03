section .text
bits 64

extern exception_handler
extern keyboard_handler
extern mouse_handler
extern pit_handler

%macro ISR_NOERRCODE 1
global isr%1
isr%1:
    cli
    push qword 0
    push qword %1
    jmp isr_common
%endmacro

%macro ISR_ERRCODE 1
global isr%1
isr%1:
    cli
    push qword %1
    jmp isr_common
%endmacro

%macro IRQ 2
global irq%1
irq%1:
    cli
    push qword 0
    push qword %2
    jmp irq_common
%endmacro

ISR_NOERRCODE 0
ISR_NOERRCODE 1
ISR_NOERRCODE 2
ISR_NOERRCODE 3
ISR_NOERRCODE 4
ISR_NOERRCODE 5
ISR_NOERRCODE 6
ISR_NOERRCODE 7
ISR_ERRCODE   8
ISR_NOERRCODE 9
ISR_ERRCODE   10
ISR_ERRCODE   11
ISR_ERRCODE   12
ISR_ERRCODE   13
ISR_ERRCODE   14

IRQ 0, 32
IRQ 1, 33
IRQ 2, 34
IRQ 3, 35
IRQ 4, 36
IRQ 5, 37
IRQ 6, 38
IRQ 7, 39
IRQ 8, 40
IRQ 9, 41
IRQ 10, 42
IRQ 11, 43
IRQ 12, 44
IRQ 13, 45
IRQ 14, 46
IRQ 15, 47

isr_common:
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    
    mov rdi, [rsp + 120]
    mov rsi, [rsp + 128]
    mov rdx, [rsp + 136]
    mov rcx, [rsp + 144]
    mov r8,  [rsp + 152]
    mov r9, rsp
    
    call exception_handler
    
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
    
    add rsp, 16
    iretq

irq_common:
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    
    mov rdi, [rsp + 120]
    
    cmp rdi, 32
    je .pit
    cmp rdi, 33
    je .keyboard
    cmp rdi, 44
    je .mouse
    jmp .done
    
.pit:
    mov rdi, rsp
    call pit_handler
    test rax, rax
    jz .done
    mov rsp, rax
    jmp .done
    
.keyboard:
    call keyboard_handler
    jmp .done

.mouse:
    call mouse_handler
    jmp .done
    
.done:
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
    
    add rsp, 16
    iretq
