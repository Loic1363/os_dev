# Linker (`linker.ld`)

## Purpose of the Linker Script

The linker script defines how kernel sections are laid out in the final binary. It ensures the bootloader jumps to the correct entry symbol and that critical sections are placed at the expected addresses.

## Key Directives

### Entry Point

The entry point tells the linker where execution begins when the bootloader transfers control to the kernel.

```ld
ENTRY(start)
```

### Sections Layout

The `SECTIONS` command places the kernel at 1 MiB and defines the sections that will be included in the final image.

```ld
SECTIONS
{
    . = 1M;

    .boot :
    {
        KEEP(*(.multiboot_header))
    }

    .text :
    {
        *(.text)
    }
}
```

## Explanation of Sections

### `. = 1M;`

The starting address for the kernel is set to 1 MiB (0x100000). This is a conventional location for kernels to be loaded by the bootloader, particularly in older BIOS systems.

```ld
. = 1M;
```

### `.boot`

The `.boot` section preserves the Multiboot header so the bootloader can detect and load the kernel properly.

- `KEEP(...)` prevents the linker from discarding the header as unused.
- `*(.multiboot_header)` pulls the header section from all object files.

```ld

.boot :
{
    KEEP(*(.multiboot_header))
}
```

### `.text`

The `.text` section contains the executable code. It starts with the multiboot header, followed by the kernel’s code.


```ld

.text :
{
    *(.text)
}
```

## Summary

This linker script places the kernel at 1 MiB, keeps the Multiboot header in a dedicated `.boot` section, and includes the kernel code in `.text`. If you add more sections (e.g., `.rodata`, `.data`, `.bss`), update this file to reflect those layouts.
