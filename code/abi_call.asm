.code

PUBLIC call_dynamic_function
call_dynamic_function PROC FRAME
    push rbp
    .pushreg rbp
    push rbx
    .pushreg rbx
    push rsi
    .pushreg rsi
    push rdi
    .pushreg rdi
    push r12
    .pushreg r12
    push r13
    .pushreg r13
    mov rbp, rsp
    .setframe rbp, 0
    .endprolog

    mov rbx, rcx        ; function
    mov rsi, rdx        ; args
    mov edi, r8d        ; arg count
    mov r12d, r9d       ; float register mask + return flag

    mov eax, edi
    sub eax, 4
    jge has_stack_args
    xor eax, eax
    jmp alloc_call_frame

has_stack_args:
    shl eax, 3

alloc_call_frame:
    add eax, 32         ; Win64 shadow space
    sub rsp, rax
    and rsp, -16
    mov r13, rsp

    cmp edi, 4
    jle load_register_args
    mov ecx, edi
    dec ecx

copy_stack_args:
    cmp ecx, 3
    jle load_register_args
    mov eax, ecx
    mov r10, qword ptr [rsi + rax*8]
    sub eax, 4
    mov qword ptr [r13 + rax*8 + 32], r10
    dec ecx
    jmp copy_stack_args

load_register_args:
    cmp edi, 1
    jl do_call
    mov rcx, qword ptr [rsi]
    test r12d, 1
    jz arg1
    movq xmm0, rcx

arg1:
    cmp edi, 2
    jl do_call
    mov rdx, qword ptr [rsi + 8]
    test r12d, 2
    jz arg2
    movq xmm1, rdx

arg2:
    cmp edi, 3
    jl do_call
    mov r8, qword ptr [rsi + 16]
    test r12d, 4
    jz arg3
    movq xmm2, r8

arg3:
    cmp edi, 4
    jl do_call
    mov r9, qword ptr [rsi + 24]
    test r12d, 8
    jz do_call
    movq xmm3, r9

do_call:
    call rbx
    bt r12d, 31
    jnc ret_integer
    movq rax, xmm0

ret_integer:
    mov rsp, rbp
    pop r13
    pop r12
    pop rdi
    pop rsi
    pop rbx
    pop rbp
    ret
call_dynamic_function ENDP

END
