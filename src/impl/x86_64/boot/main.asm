global start 

section .text
bits 32
start: 
    mov esp, stack_top

    call check_multiboot
    call check_cpuid
    call check_long_mode

    ; print 'OK'
    mov dword [0xb8000], 0x2f4b2f4f
    hlt ; halt CPU for no more instructions


check_multiboot:
    cmp eax, 0x36d76289
    jne .no_multiboot ; jump if not equal = jne (like brne)
    ret ; return = ret
.no_multiboot:
    mov  al, "M"
    jmp error 

check_cpuid:
    pushfd 
    pop eax
    mov ecx, eax
    xor eax, 1 << 21
    push eax
    popfd
    pushfd
    pop eax
    push ecx
    popfd
    cmp eax, ecx ; compare = cmp
    je .no_cpuid
    ret

.no_cpuid:
    mov al, "C"
    jmp error

check_long_mode:
    

error:
    ; print "ERR: X" where X is the error code
    mov dword [0xb8000], 0x4f524f45
    mov dword [0xb8004], 0x4f524f45
    mov dword [0xb8008], 0x4f524f45
    mov byte  [0xb800a], al
    hlt 

section .bss
stack_bottom:
    resb 4096 * 4
stack_top: