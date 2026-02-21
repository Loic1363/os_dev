section .multiboot_header ; name "multiboot_header" for this section
header_start:
    ; magic number (that multiboot looking for)
    dd 0xe85250d6 ; multiboot2
    dd 0 ; protected mode i386
    dd header_end - header_start ; header length
    dd 0x100000000 - (0xe85250d6 + 0 + (header_end - header_start)); checksum

    ; end tag
    dw 0
    dw 0
    dd 8
header_end:
