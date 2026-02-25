global start ; make "start" visible to the linker (otherwise it is local)
extern long_mode_start ; declared in another file (exported there as global long_mode_start)
; ============================================================
section .text
bits 32 ; assemble in 32-bit mode
start: ; linker entry point via ENTRY(start)

    mov esp, stack_top ; initialize the stack (ESP = 32-bit stack pointer)
; load stack_top into ESP

; ============================================================
;   .bss memory layout (stack + page tables)
;   The x86 stack grows downward (ESP--) on each push/call
;
;   High addresses
;           |
;           v
;
;        +-------------------------+  <- stack_top (ESP initial)
;        |         STACK           |
;        |   (16 KB reserved)      |
;        |     push / call ↓       |
;        |                         |
;        |                         |
;        +-------------------------+  <- stack_bottom
;        |      page_table_l2      |
;        +-------------------------+
;        |      page_table_l3      |
;        +-------------------------+
;        |      page_table_l4      |
;        +-------------------------+
;
;   Low addresses
; ============================================================
; call setup/check routines defined below (start behaves like a boot main)
    call check_multiboot 
    call check_cpuid
    call check_long_mode

    call setup_page_tables
    call enable_paging

; ============================================================

    lgdt   [gdt64.pointer] ; load the GDT pointer from memory into GDTR

; GDT  = Global Descriptor Table (stores segment descriptors in table form)
; GDTR = Global Descriptor Table Register (internal register containing:
;                                          GDTR.base   = address of the GDT in memory
;                                          GDTR.limit  = size of the GDT - 1)










    jmp gdt64.code_segment:long_mode_start ; switch to long mode (64-bit)

    hlt ; halt CPU for no more instructions
; ============================================================


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
    mov eax, 0x80000000
    cpuid
    cmp eax, 0x80000001
    jb .no_long_mode

    mov eax, 0x80000001
    cpuid
    test edx, 1 << 29
    jz .no_long_mode

    ret

.no_long_mode:
    mov al, "L"
    jmp error

setup_page_tables:
    mov eax, page_table_l3
    or eax, 0b11 ; present, writable
    mov [page_table_l4], eax

    mov eax, page_table_l2
    or eax, 0b11 ; present, writable
    mov [page_table_l3], eax

    mov ecx, 0 ; counter

.loop:

    mov eax, 0x200000 ; 2MiB
    mul ecx
    or eax, 0b10000011 ; present, writable, huge page
    mov [page_table_l2 + ecx * 8], eax

    inc ecx ; increment counter
    cmp ecx, 512 ; checks if the whole table is mapped 
    jne .loop  ; if not, continue 

    ret

enable_paging:
    ; pass page table location to CPU
    mov eax, page_table_l4
    mov cr3, eax

    ; enable PAE
    mov eax, cr4
    or eax, 1 << 5
    mov cr4, eax

    ; enable long mode
    mov ecx, 0xC0000080
    rdmsr
    or eax, 1 << 8
    wrmsr

    ; enable paging 
    mov eax, cr0
    or eax, 1 << 31
    mov cr0, eax

    ret

error:
    ; print "ERR: X" where X is the error code
    mov dword [0xb8000], 0x4f524f45
    mov dword [0xb8004], 0x4f524f45
    mov dword [0xb8008], 0x4f524f45
    mov byte  [0xb800a], al
    hlt 

section .bss
align 4096
page_table_l4:
    resb 4096 
page_table_l3:
    resb 4096 
page_table_l2:
    resb 4096 
stack_bottom:
    resb 4096 * 4
stack_top:


section .rodata
gdt64:
    dq 0 ; 0 entry
.code_segment: equ $ - gdt64
    dq (1 << 43) | (1 << 44) | (1 << 47) | (1 << 53); code segment
.pointer:
    dw $ - gdt64 - 1
    dq gdt64
