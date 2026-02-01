bits 64
section .text

global enter_usermode
global switch_task

enter_usermode:
    cli
    mov ax, 0x23
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    push 0x23
    push rsi
    pushfq
    or qword [rsp], 1<<9
    push 0x1B
    push rdi
    xor rax,rax
    xor rbx,rbx
    xor rcx,rcx
    xor rdx,rdx
    xor rsi,rsi
    xor rdi,rdi
    xor rbp,rbp
    xor r8,r8
    xor r9,r9
    xor r10,r10
    xor r11,r11
    xor r12,r12
    xor r13,r13
    xor r14,r14
    xor r15,r15
    iretq

switch_task:
    pushfq
    cli
    test rdi,rdi
    jz .load

    mov [rdi+0],rax
    mov [rdi+8],rbx
    mov [rdi+16],rcx
    mov [rdi+24],rdx
    mov [rdi+32],rsi
    mov [rdi+40],rdi
    mov [rdi+48],rbp
    mov [rdi+56],rsp
    mov [rdi+64],r8
    mov [rdi+72],r9
    mov [rdi+80],r10
    mov [rdi+88],r11
    mov [rdi+96],r12
    mov [rdi+104],r13
    mov [rdi+112],r14
    mov [rdi+120],r15

    mov rax,[rsp+8]
    mov [rdi+128],rax

    pop rax
    mov [rdi+136],rax

    xor rax,rax
    mov ax,cs
    mov [rdi+144],rax
    xor rax,rax
    mov ax,ss
    mov [rdi+152],rax
    mov rax,cr3
    mov [rdi+304],rax

.load:
    mov rax,[rsi+304]
    test rax,rax
    jz .noc3
    mov cr3,rax
.noc3:
    mov rax,[rsi+144]
    cmp rax, 0x1B
    je .load_user

    mov rax,[rsi+0]
    mov rbx,[rsi+8]
    mov rcx,[rsi+16]
    mov rdx,[rsi+24]
    mov r8,[rsi+64]
    mov r9,[rsi+72]
    mov r10,[rsi+80]
    mov r11,[rsi+88]
    mov r12,[rsi+96]
    mov r13,[rsi+104]
    mov r14,[rsi+112]
    mov r15,[rsi+120]
    mov rbp,[rsi+48]
    mov rsp,[rsi+56]

    mov rdi,[rsi+40]
    mov rax,[rsi+32]
    mov rsi,rax

    popfq
    ret

.load_user:
    mov rax,[rsi+0]
    mov rbx,[rsi+8]
    mov rcx,[rsi+16]
    mov rdx,[rsi+24]
    mov r8,[rsi+64]
    mov r9,[rsi+72]
    mov r10,[rsi+80]
    mov r11,[rsi+88]
    mov r12,[rsi+96]
    mov r13,[rsi+104]
    mov r14,[rsi+112]
    mov r15,[rsi+120]
    mov rbp,[rsi+48]

    push qword [rsi+152]
    push qword [rsi+56]
    push qword [rsi+136]
    push qword [rsi+144]
    push qword [rsi+128]

    mov rdi,[rsi+40]
    mov rax,[rsi+32]
    mov rsi,rax

    iretq