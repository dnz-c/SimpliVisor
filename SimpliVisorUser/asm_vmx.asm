.code

; --- rcx = reason, rdx = arg1, r8 = arg2, r9 = arg3, r10 (rsp+40) = arg4
PUBLIC asm_vmcall
asm_vmcall PROC
    mov r10, [rsp + 40]
    vmcall
    ret
asm_vmcall ENDP

END