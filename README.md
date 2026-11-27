# CM-OS
*CM-OS Alper Pektas tarafından KURUMLARDAN bağımsız olarak geliştirilmiştir.*
[English](#english)
A bare-metal 32-bit x86 operating system driven by **CM**, a math- and logic-based programming language. 
Built around memory interval partitioning, inter-region passing 
(`>-<` merging, `L`/`J` center transfers, and `3-2-3` guide streams), a `1024x768` VESA tiling UI, a 
`"kullanici"@cm>` shell, and the `cme` text editor.

## Architecture

- **Memory Pool & Interval Passing:** 500 KB pool (`EBP = 0x100000`) split across 5 regions (`left`, `op1`, `middle`, `op2`, `right`), supporting dynamic interval expansion/merging (`>-<`), `L`/`J` center transfers, and `3-2-3` guide parsing.
- **Graphics & Tiling UI:** VESA VBE `1024x768` (32-bit LFB) rendering, 8x8 bitmap font engine, dynamic window borders, and top Wibar state indicators.
- **Shell & Editor:** PS/2 (`0x64` / `0x60`) keyboard driver, `"kullanici"@cm>` command loop (`print`, `ls`, `cd`, `merge`, `topmerge`, `clear`, `reset`), and `cme` editor (`Esc` to save and return).

## Build and Run (QEMU)

```bash
gcc -O2 cm_boot.c -o cm_boot && ./cm_boot cm_os.bin
qemu-system-i386 -drive format=raw,file=cm_os.bin

[Türkçe](#türkçe-özet)
Türkçe Özet

CM-OS, matematik ve mantık tabanlı CM programlama diliyle işletilen; 
bellek aralıklandırma ve bölgeler arası paslaşma (>-< birleştirme, L/J merkez aktarımları, 3-2-3 kılavuz akışı), 
1024x768 VESA döşemeli pencere arayüzü, "kullanici"@cm> kabuğu ve cme metin düzenleyicisine 
sahip bare-metal 32-bit x86 işletim sistemidir.

Derleme ve QEMU ile Çalıştırma:

gcc -O2 cm_boot.c -o cm_boot && ./cm_boot cm_os.bin
qemu-system-i386 -drive format=raw,file=cm_os.bin
