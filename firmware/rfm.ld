/* SPDX-License-Identifier: GPL-3.0-only */
/* RAVE FM 2's memory map: the platform's (app.ld), without its Bluetooth segment and its pinned audio layout.
 * Code and constants run in place from XIP (app.bin offset 0 = 0x02000120): first the start, then the interrupts and
 * everything they reach (.text.a.*: tools/build_ravefm.py renames them; the I-cache sees one block of the sound, and
 * the RAM code's calls stay within their +-4 MiB reach), then the rest.
 * RAMTEXT: the flash driver's RAM code (.ram_text), the sound's hottest code (.hot_text).
 * RAM: .data/.bss (96 KiB, every DMA reaches it). POOL: the big zeroed buffers (.pool: RAVE FM's sound state).
 * LOWBUF: main-loop buffers. NOINIT survives resets and UBOOT entry. 0x01C7FD50.. is reserved (boot info, mailbox,
 * vectors). */
MEMORY
{
    XIP    (rx)  : ORIGIN = 0x02000120, LENGTH = 0x8EA90   /* = tools/fm1pkg_make.py APP_SLOT */
    LOWBUF (rw)  : ORIGIN = 0x01C00000, LENGTH = 0x8000
    RAM    (rwx) : ORIGIN = 0x01C08000, LENGTH = 96K
    POOL   (rw)  : ORIGIN = 0x01C20000, LENGTH = 0x49800   /* (16 KiB of it given to RAMTEXT: the synth, the break, the
                                                          * 909 in RAM, not read from flash in the interrupt) */
    RAMTEXT (rwx): ORIGIN = 0x01C69800, LENGTH = 0xC800
    NOINIT (rw)  : ORIGIN = 0x01C7C000, LENGTH = 0x3D50
    PACKED (r)   : ORIGIN = 0x02200000, LENGTH = 0x10000  /* the load images of .ram_text, .hot_text, .data, never in
                                                           * flash: packed (LZ4) after .text (_etext), unpacked by
                                                           * fm1_cstart (src/boot_lz4.h) */
}
SECTIONS
{
    .text : { KEEP(*(.text.crt0)) *(.text.isr_*) *(SORT(.text.a.*)) *(.rodata.a.*) *(.text) *(.text*)
              *(.rodata) *(.rodata.cst*) *(.rodata.str*) *(.rodata*)
              . = ALIGN(4); _etext = .; } > XIP
    .ram_text : { _rt_start = .; KEEP(*(.ram_text*)) . = ALIGN(4); _rt_end = .; } > RAMTEXT AT > PACKED
    _rt_load = LOADADDR(.ram_text);
    .hot_text : { _hot_start = .; KEEP(*(.c1_text*)) KEEP(*(.hot_text*)) . = ALIGN(4); _hot_end = .; } > RAMTEXT AT > PACKED
                                                          /* (with the second core's waiting loop, which calls its jobs:
                                                           *  not in .ram_text, the code that runs with the flash off) */
    _hot_load = LOADADDR(.hot_text);
    .data : { . = ALIGN(4); _data_start = .; *(.data*) *(.sdata*) . = ALIGN(4); _data_end = .; } > RAM AT > PACKED
    _data_load = LOADADDR(.data);
    .bss (NOLOAD) : { _bss_start = .; *(.bss*) *(.sbss*) *(COMMON) . = ALIGN(4); _bss_end = .; } > RAM
    .pool (NOLOAD) : { _pool_start = .; *(.pool*) . = ALIGN(4); _pool_end = .; } > POOL
    .lowbuf (NOLOAD) : { _low_start = .; *(.lowbuf*) . = ALIGN(4); _low_end = .; } > LOWBUF
    .c1_stacks (NOLOAD) : { . = ALIGN(32); _c1_stack_lo = .; . += 7168; _c1_stack_hi = .; } > LOWBUF   /* core 1's
                                                          * (the pool is full; LOWBUF had 30 KiB unused) */
    _c1_ustack_top = _c1_stack_lo + 6144;
    _c1_sstack_top = _c1_stack_hi;
    .noinit (NOLOAD) : { *(.noinit) *(.noinit.*) } > NOINIT
    _bt_end = 0;                                          /* (no Bluetooth segment: fm1_guard.h's PC window ends at _etext) */
    _c1t_end = _hot_start;
    _guard0 = 0x01C76000; _ustack_lo = 0x01C76100; _ustack_top = 0x01C7A000;
    _guard1 = 0x01C7A000; _sstack_lo = 0x01C7A100; _sstack_top = 0x01C7C000;
    /DISCARD/ : { *(.comment) *(.note*) }
}
