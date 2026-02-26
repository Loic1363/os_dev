# PipOS

PipOS is a small x86_64 hobby kernel with a text-mode interface, a built-in shell, a RAM-backed filesystem, and a custom text editor (`nat`).

It is written mostly in C, with a small amount of x86_64 assembly for boot and interrupt/exception entry stubs.

## Screenshot

Boot / shell startup screen:

![PipOS loading screen](ext_src/pic/loading_screen.png)

## Features

- x86_64 boot flow (32-bit setup to 64-bit kernel)
- VGA text-mode output
- IDT + PIC initialization
- IRQ handling:
  - PIT timer (`IRQ0`)
  - PS/2 keyboard (`IRQ1`)
- CPU exception handlers with panic output:
  - `#DE` (Divide Error)
  - `#GP` (General Protection Fault)
  - `#PF` (Page Fault)
- RTC time access (`HH:MM:SS`)
- Interactive shell-like console:
  - command history (`Up` / `Down`)
  - `Tab` completion (commands + paths, unique-match)
  - colored prompt (`admin:~$`)
  - top status line (`PIT` + `RTC`)
  - `Ctrl+L` clear
- In-memory filesystem (RAM FS)
- `nat` text editor (Nano-like shortcuts, PipOS-style UI)

## Shell Commands (Current)

- `help`, `help <command>`
- `clear`, `cls`
- `ticks`, `time`, `uptime`
- `about`, `version`
- `echo`
- `pwd`, `ls`, `tree`, `cd`
- `mkdir`, `touch`, `mv`, `cp`
- `cat`, `rm`, `rmdir`, `stat`
- `history`
- `nat <file>`
- exception tests:
  - `panic_de`
  - `panic_gp`
  - `panic_pf`

## `nat` Editor (Current Shortcuts)

- `Ctrl+O` write/save
- `Ctrl+X` exit
- `Ctrl+S` save (alias)
- `Ctrl+Q` quit (alias)
- `Ctrl+K` cut line
- `Ctrl+U` uncut / paste line
- `Ctrl+A` line start
- `Ctrl+E` line end
- `Ctrl+L` refresh
- `Ctrl+G` help hint
- `Ctrl+C` cursor position
- `Ctrl+W` search placeholder
- arrows, `Backspace`, `Enter`, `Tab`

## Project Layout

```text
.
├── boot.sh
├── buildenv/
├── ext_src/
│   └── pic/
│       └── loading_screen.png
├── src/
│   ├── impl/
│   │   ├── kernel/
│   │   │   ├── main.c
│   │   │   ├── console.c
│   │   │   ├── apps/
│   │   │   │   └── nat.c
│   │   │   └── lib/x86_64/functions/
│   │   │       ├── ramfs.c
│   │   │       ├── string.c
│   │   │       └── tokenizer.c
│   │   └── x86_64/
│   │       ├── boot/
│   │       ├── idt.c
│   │       ├── idt_.asm
│   │       ├── pic.c
│   │       ├── pit.c
│   │       ├── print.c
│   │       ├── ps2.c
│   │       ├── rtc.c
│   │       └── ...
│   └── intf/
│       ├── apps/
│       │   └── nat.h
│       ├── console.h
│       ├── print.h
│       └── lib/x86_64/functions/
│           ├── ramfs.h
│           ├── string.h
│           └── tokenizer.h
└── targets/
```

## In-Kernel RAM FS (Example Tree)

The shell operates on a RAM-backed filesystem created at boot (not your host filesystem).

```text
/
├── bin/
├── home/
│   └── admin/
├── lib/
│   └── x86_64/
│       └── functions/
│           ├── idt.c
│           ├── port.c
│           └── print.c
└── tmp/
```

## Build and Run

### Requirements

- Docker
- `qemu-system-x86_64`

### Build the Docker image (once)

```bash
sudo docker build buildenv -t myos-buildenv
```

### One-command run (recommended)

```bash
./boot.sh
```

This script:

1. Builds the kernel and ISO inside Docker
2. Launches QEMU

### Manual build / run

```bash
sudo docker run --rm -t -v "$(pwd):/root/env" myos-buildenv bash -lc "make build-x86_64"
qemu-system-x86_64 -cdrom dist/x86_64/kernel.iso -m 256M -display sdl
```

## Notes

- The shell filesystem is currently RAM-only (no disk persistence yet).
- `nat` edits files inside the in-kernel RAM FS.
- The keyboard layout is Belgian AZERTY (ASCII-oriented in VGA text mode).

## Roadmap

- real search in `nat` (`Ctrl+W`)
- file content helpers (`write`, `append`, `grep`)
- recursive copy / delete in RAM FS
- serial COM1 logging
- disk-backed filesystem support (FAT, read-only first)

## License

MIT. See [`LICENSE`](LICENSE).
