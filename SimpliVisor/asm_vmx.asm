.code

EXTERN enter_vmx_operation : PROC
EXTERN vmexit_handler : PROC

PUBLIC asm_virtualize_core
asm_virtualize_core PROC
	; --- 1. Capture GPRs ---
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

	; --- 2. Prepare enter_vmx_operation call
	; RCX already contains the core index
	; RDX needs to be the stack pointer
	mov rdx, rsp

	sub rsp, 20h ; Shadow space
	call enter_vmx_operation
	add rsp, 20h

	; --- 3. enter_vmx_operation should call vmlaunch if we are here we have a problem
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
    
    xor rax, rax   ; Return FALSE (0) to the driver
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
	; --- 1. Capture GPRs ---
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

	; --- 2. Prepare vmexit_handler call
	; RCX already contains the core index
	; RDX needs to be the stack pointer
	mov rcx, rsp

	sub rsp, 20h ; Shadow space
	call vmexit_handler
	add rsp, 20h

	; --- 3. check the return type optionally
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