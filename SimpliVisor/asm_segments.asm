.code
; --- https://github.com/SinaKarvandi/Hypervisor-From-Scratch/blob/master/Part%206%20-%20Virtualizing%20An%20Already%20Running%20System/MyHypervisorDriver/MyHypervisorDriver/Main.asm

PUBLIC get_gdt_base
get_gdt_base PROC

	local	gdtr[10]:byte
	sgdt	gdtr
	mov		rax, qword ptr gdtr[2]

	ret

get_gdt_base ENDP

PUBLIC get_cs
get_cs PROC

	mov		rax, cs
	ret

get_cs ENDP

PUBLIC get_ds
get_ds PROC

	mov		rax, ds
	ret

get_ds ENDP

PUBLIC get_es
get_es PROC

	mov		rax, es
	ret

get_es ENDP

PUBLIC get_ss
get_ss PROC

	mov		rax, ss
	ret

get_ss ENDP

PUBLIC get_fs
get_fs PROC

	mov		rax, fs
	ret

get_fs ENDP

PUBLIC get_gs
get_gs PROC

	mov		rax, gs
	ret

get_gs ENDP

PUBLIC get_ldtr
get_ldtr PROC

	sldt	rax
	ret

get_ldtr ENDP

PUBLIC get_tr
get_tr PROC

	str	rax
	ret

get_tr ENDP

PUBLIC get_idt_base
get_idt_base PROC

	local	idtr[10]:byte
	
	sidt	idtr
	mov		rax, qword ptr idtr[2]

	ret

get_idt_base ENDP

PUBLIC get_gdt_limit
get_gdt_limit PROC

	local	gdtr[10]:byte

	sgdt	gdtr
	mov		ax, word ptr gdtr[0]

	ret

get_gdt_limit ENDP

PUBLIC get_idt_limit
get_idt_limit PROC

	local	idtr[10]:byte
	
	sidt	idtr
	mov		ax, word ptr idtr[0]

	ret

get_idt_limit ENDP

PUBLIC get_rflags
get_rflags PROC

	pushfq
	pop		rax

	ret

get_rflags ENDP

END