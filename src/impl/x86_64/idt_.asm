extern idt_handler_exception_de
extern idt_handler_exception_gp
extern idt_handler_exception_pf
extern idt_handler_timer
extern idt_handler_keyboard

global idt_load

idt_load:
	lidt [rdi]
	ret

%macro WRAPPED_HANDLER 1
	global %1_wrapped

	%1_wrapped:
		; save general-purpose registers
		push rax
		push rbx
		push rcx
		push rdx
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
		call %1
		; restore general-purpose registers
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
		pop rdx
		pop rcx
		pop rbx
		pop rax

		iretq
%endmacro

%macro WRAPPED_HANDLER_ERRCODE 1
	global %1_wrapped

	%1_wrapped:
		; save general-purpose registers
		push rax
		push rbx
		push rcx
		push rdx
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

		; error code is below the 15 pushed registers
		mov rdi, [rsp + 15 * 8]
		call %1

		; restore general-purpose registers
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
		pop rdx
		pop rcx
		pop rbx
		pop rax

		add rsp, 8 ; discard CPU-pushed error code
		iretq
%endmacro

WRAPPED_HANDLER idt_handler_exception_de
WRAPPED_HANDLER_ERRCODE idt_handler_exception_gp
WRAPPED_HANDLER_ERRCODE idt_handler_exception_pf
WRAPPED_HANDLER idt_handler_timer
WRAPPED_HANDLER idt_handler_keyboard
