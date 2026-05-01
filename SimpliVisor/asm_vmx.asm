.code

EXTERN enter_vmx_operation : PROC
EXTERN vmexit_handler : PROC

; --- invalidate the ept tlb
; rcx = type 1: single ctx, 2: all ctx, rdx = pointer to 128bit invept descriptor 
PUBLIC asm_invept
asm_invept PROC
    invept rcx, oword ptr [rdx]
    ret
asm_invept ENDP

; --- exit vmx operation
; rcx = rip, rdx = rsp, r8 = rflags, r9 = cs, rsp+40 = ss, rsp+48 = pguest_regs
PUBLIC asm_exit_vm
asm_exit_vm PROC
    mov r10, [rsp + 40] ; r10 = ss
    mov rax, [rsp + 48] ; rax = pguest_regs

    ; build iretq stack frame
    push r10 ; ss
    push rdx ; rsp
    push r8 ; rflags
    push r9 ; cs
    push rcx ; rip

    ; restore guest GPRs
    mov r15, [rax + 0]
    mov r14, [rax + 8]
    mov r13, [rax + 16]
    mov r12, [rax + 24]
    mov r11, [rax + 32]
    mov r10, [rax + 40]
    mov r9,  [rax + 48]
    mov r8,  [rax + 56]
    mov rdi, [rax + 64]
    mov rsi, [rax + 72]
    mov rbp, [rax + 80]
    ; skip rax + 88 (rsp placeholder) since iretq will handle the rsp
    mov rbx, [rax + 96]
    mov rdx, [rax + 104]
    mov rcx, [rax + 112]

    ; restore rax since we no longer need the pguest_regs pointer
    mov rax, [rax+ 120]

    ; return to the OS with vmxoff
    iretq
asm_exit_vm ENDP

; --- rcx = reason, rdx = arg1, r8 = arg2, r9 = arg3, r10 (rsp+40) = arg4
PUBLIC asm_vmcall
asm_vmcall PROC
    mov r10, [rsp + 40]
    vmcall
    ret
asm_vmcall ENDP

PUBLIC asm_virtualize_core
asm_virtualize_core PROC
	; --- 1 capture GPRs
	pushfq
	push rax
	push rcx
	push rdx
	push rbx
	push rbp ; RSP placeholder
	push rbp
	push rsi
	push rdi
	push r8
	push r9
	push r10
	push r11
	push r12
	push r13
	push r14
	push r15

	; --- prepare enter_vmx_operation call
	mov rdx, rsp

	sub rsp, 20h ; Shadow space
	call enter_vmx_operation
	add rsp, 20h

	; --- enter_vmx_operation should call vmlaunch if we are here we have a problem
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rbp
    pop rbx ; discard RSP placeholder
    pop rbx
    pop rdx
    pop rcx
    pop rax
    popfq
    
    xor rax, rax
    ret
asm_virtualize_core ENDP

PUBLIC asm_vmx_restore_state
asm_vmx_restore_state PROC
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rbp
    pop rbx ; discard RSP placeholder
    pop rbx
    pop rdx
    pop rcx
    pop rax
    popfq

    mov rax, 1
    ret
asm_vmx_restore_state ENDP

PUBLIC asm_vmexit_handler
asm_vmexit_handler PROC
	; --- capture GPRs
	pushfq
	push rax
	push rcx
	push rdx
	push rbx
	push rbp ; RSP placeholder
	push rbp
	push rsi
	push rdi
	push r8
	push r9
	push r10
	push r11
	push r12
	push r13
	push r14
	push r15

	; --- prepare vmexit_handler call
	mov rcx, rsp

	sub rsp, 20h ; Shadow space
	call vmexit_handler
	add rsp, 20h

	; --- check the return type optionally
    test rax, rax ; --- ideally we would jump to the routine that exits vmx operation and restores the host state here

    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rbp
    pop rbx ; discard RSP placeholder
    pop rbx
    pop rdx
    pop rcx
    pop rax
    popfq
    
    vmresume

    ; --- if we are here we couldnt resume the VM this is a really bad error
    mov r9, 4400h ; VM_INSTRUCTION_ERROR
    vmread rax, r9
    jmp $
asm_vmexit_handler ENDP

END