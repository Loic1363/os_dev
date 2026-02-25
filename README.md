# PipOS (x86_64 Hobby Kernel)

PipOS is a small 64-bit hobby operating system kernel written mostly in C with a small amount of x86_64 assembly for boot and interrupt/exception entry stubs.

This project started from the early tutorial codebase by **David Callanan (@CodePulse)** and has since been extended/refactored with new kernel features (interrupts, PIT/RTC, console shell, in-memory filesystem, status line, exception handlers, etc.).

## Attribution

The initial boot/kernel foundation comes from the "Write Your Own 64-bit Operating System Kernel From Scratch" tutorial series by David Callanan:

- YouTube playlist: <https://www.youtube.com/playlist?list=PLZQftyCk7_SeZRitx5MjBKzTtvk0pHMtp>
- Original repository / revisions: <https://github.com/davidcallanan/yt-os-series>

This repository is an independent derivative project with additional features and structural changes.

## Current Features

- 32-bit boot code -> switch to 64-bit long mode
- VGA text-mode output (80x25)
- IDT + PIC initialization
- IRQ handlers:
  - PIT timer (IRQ0)
  - PS/2 keyboard (IRQ1)
- CPU exception handlers with panic screen:
  - `#DE` Divide Error
  - `#GP` General Protection Fault
  - `#PF` Page Fault
- RTC time reads (`HH:MM:SS`)
- Interactive shell-like console:
  - command history (`Up` / `Down`)
  - software blinking cursor
  - `Ctrl+L` to clear screen
- In-memory filesystem (RAM-only, no disk persistence yet)

## Project Layout

```text
src/
├── impl/
│   ├── kernel/
│   │   ├── main.c
│   │   ├── console.c
│   │   └── lib/x86_64/functions/
│   │       ├── ramfs.c
│   │       ├── string.c
│   │       └── tokenizer.c
│   └── x86_64/
│       ├── boot/
│       ├── idt.c
│       ├── idt_.asm
│       ├── pic.c
│       ├── pit.c
│       ├── print.c
│       ├── ps2.c
│       ├── rtc.c
│       └── ...
└── intf/
    ├── console.h
    ├── print.h
    └── lib/x86_64/functions/
        ├── ramfs.h
        ├── string.h
        └── tokenizer.h
```

## In-Kernel RAM Filesystem (Demo Tree)

The shell currently operates on a built-in in-memory tree (seeded at boot). Example:

```text
/
├── bin/
├── home/
│   └── user/
├── lib/
│   └── x86_64/
│       └── functions/
│           ├── idt
│           ├── port
│           └── print
└── tmp/
```

## Build Requirements

- Docker
- QEMU (`qemu-system-x86_64`)

The Docker image contains the cross-toolchain and build dependencies (`x86_64-elf-gcc`, `ld`, `nasm`, GRUB tools).

## Quick Start (Recommended)

Build the Docker image once:

```bash
sudo docker build buildenv -t myos-buildenv
```

Then use the one-command helper script:

```bash
./boot.sh
```

`boot.sh` will:

1. Build the kernel + ISO inside Docker
2. Launch QEMU with the generated ISO

## Manual Build / Run (Alternative)

```bash
sudo docker run --rm -t -v "$(pwd):/root/env" myos-buildenv bash -lc "make build-x86_64"
qemu-system-x86_64 -cdrom dist/x86_64/kernel.iso -m 256M -display sdl
```

## Keyboard Notes

The console uses a **Belgian AZERTY** key mapping (work-in-progress). Some non-ASCII Belgian characters are mapped to ASCII fallbacks in VGA text mode.

## Roadmap (Short)

- `rm` / `rmdir`
- file contents in RAM FS (`cat`, write support)
- better line editing (`Left` / `Right`, `Delete`)
- serial COM1 logging
- disk-backed filesystem (FAT read-only first)

## License

This project is licensed under the **MIT License**. See [`LICENSE`](LICENSE).
