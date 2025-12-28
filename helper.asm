; Windows x64 Calling Convention
; RCX = func_ptr, RDX = args[], R8D = arg_count, R9D = float_mask

.code

call_dynamic_function PROC
    ; --- Prologue ---
    push rbp            ; Save old frame pointer
    push rbx            ; Save non-volatile registers
    push rsi
    push rdi
    push r12
    push r13
    mov  rbp, rsp       ; Set up our frame pointer after pushes
    
    ; Now, [rbp] points to the saved r13. 
    ; The stack is currently 8-byte aligned (because we pushed 6 regs + rbp = 7 total).

    ; Save inputs into non-volatiles
    mov rbx, rcx        ; func_ptr
    mov rsi, rdx        ; args[]
    mov edi, r8d        ; arg_count
    mov r12d, r9d       ; float_mask

    ; --- Calculate Stack Space ---
    ; 32 bytes (shadow) + max(0, (arg_count - 4) * 8)
    mov eax, edi
    sub eax, 4
    jge has_stack_args
    xor eax, eax
    jmp calc_total

has_stack_args:
    shl eax, 3          ; (arg_count - 4) * 8

calc_total:
    add eax, 32         ; Shadow space
    
    ; --- The "Magic" Alignment ---
    ; We need RSP to be 16-byte aligned before the 'call'.
    ; We subtract our space, then force the alignment.
    sub rsp, rax
    and rsp, -16        ; Force 16-byte alignment
    
    mov r13, rsp        ; r13 is now the base of our call's shadow space

    ; --- Push Stack Arguments (Arg 5+) ---
    cmp edi, 4
    jle load_register_args

    mov ecx, edi
    dec ecx             ; Zero-indexed last arg

stack_loop:
    cmp ecx, 3
    jle load_register_args

    mov rax, rcx
    shl rax, 3
    mov r10, qword ptr [rsi + rax]

    mov rax, rcx
    sub rax, 4
    shl rax, 3
    add rax, 32
    mov qword ptr [r13 + rax], r10

    dec ecx
    jmp stack_loop

    ; --- Load Register Arguments (1-4) ---
load_register_args:
    ; Arg 0
    cmp edi, 1
    jl do_call
    mov rcx, qword ptr [rsi] ; Always load GPR with the value
    test r12d, 1
    jnz arg0_float
    jmp arg1
arg0_float:
    movq xmm0, rcx             ; Load XMM from RCX (which holds the value)

arg1: ; Arg 1
    cmp edi, 2
    jl do_call
    mov rdx, qword ptr [rsi + 8] ; Always load GPR with the value
    test r12d, 2
    jnz arg1_float
    jmp arg2
arg1_float:
    movq xmm1, rdx             ; Load XMM from RDX (which holds the value)

arg2: ; Arg 2
    cmp edi, 3
    jl do_call
    mov r8, qword ptr [rsi + 16] ; Always load GPR with the value
    test r12d, 4
    jnz arg2_float
    jmp arg3
arg2_float:
    movq xmm2, r8              ; Load XMM from R8 (which holds the value)

arg3: ; Arg 3
    cmp edi, 4
    jl do_call
    mov r9, qword ptr [rsi + 24] ; Always load GPR with the value
    test r12d, 8
    jnz arg3_float
    jmp do_call
arg3_float:
    movq xmm3, r9              ; Load XMM from R9 (which holds the value)

    ; --- The Call ---
do_call:
    call rbx

    ; --- Epilogue ---
    mov rsp, rbp        ; Clean up dynamic stack & restore RSP to saved regs
    pop r13             ; Pop in reverse order of pushes
    pop r12
    pop rdi
    pop rsi
    pop rbx
    pop rbp
    ret
call_dynamic_function ENDP

END