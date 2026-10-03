EXTERN dispatch_script_callback:PROC

.code
PUBLIC script_callback_router

; Every trampoline places its slot in r10d then tail-jumps here.
script_callback_router PROC FRAME
    sub rsp, 0A8h
    .allocstack 0A8h
    .endprolog

    ; RawCallbackFrame starts at rsp+20h, after our outbound shadow space.
    mov qword ptr [rsp+20h], rcx
    mov qword ptr [rsp+28h], rdx
    mov qword ptr [rsp+30h], r8
    mov qword ptr [rsp+38h], r9
    movq qword ptr [rsp+40h], xmm0
    movq qword ptr [rsp+48h], xmm1
    movq qword ptr [rsp+50h], xmm2
    movq qword ptr [rsp+58h], xmm3

    ; Original rsp = current rsp + A8h. Arg #5 begins at original rsp + 28h.
    lea rax, [rsp+0D0h]
    mov qword ptr [rsp+60h], rax

    mov dword ptr [rsp+68h], 0       ; floating_return out parameter
    mov dword ptr [rsp+6Ch], r10d    ; preserve slot

    mov ecx, dword ptr [rsp+6Ch]
    lea rdx, [rsp+20h]
    lea r8, [rsp+68h]
    call dispatch_script_callback

    cmp dword ptr [rsp+68h], 0
    je callback_integer_return
    movq xmm0, rax

callback_integer_return:
    add rsp, 0A8h
    ret
script_callback_router ENDP

END
