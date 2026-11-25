/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

/*
 * Memory map for NXP/Freescale MIMXRT1062DVJ6A on Teensy 4.1:
 *   - FLASH  : 8 MB QSPI FlexSPI NOR Flash @ 0x60000000
 *   - ITCM   : 256 KB tightly-coupled instruction RAM @ 0x00000000
 *   - DTCM   : 256 KB tightly-coupled data RAM @ 0x20000000 (stack, .data, .bss)
 *   - OCRAM2 : 512 KB on-chip RAM @ 0x20200000 (DMAMEM: ENET/USB DMA buffers, VT/lwIP pools)
 *   - EXTMEM : Up to 16 MB external FlexSPI2 QSPI PSRAM @ 0x70000000 (optional, runtime tested)
 */

MEMORY
{
    FLASH  (rx)  : ORIGIN = 0x60000000, LENGTH = 8128K
    ITCM   (rwx) : ORIGIN = 0x00000000, LENGTH = 256K
    DTCM   (rwx) : ORIGIN = 0x20000000, LENGTH = 256K
    OCRAM2 (rwx) : ORIGIN = 0x20200000, LENGTH = 512K
    EXTMEM (rwx) : ORIGIN = 0x70000000, LENGTH = 16M
}

ENTRY(Reset_Handler)

SECTIONS
{
    .text.boot :
    {
        KEEP(*(.flashconfig))
        . = ALIGN(0x1000);
        KEEP(*(.ivt))
        KEEP(*(.bootdata))
        KEEP(*(.vectors))
    } > FLASH

    .text :
    {
        . = ALIGN(4);
        *(.text .text.*)
        *(.rodata .rodata.*)
        . = ALIGN(4);
        _etext = .;
    } > FLASH

    .data : AT (_etext)
    {
        . = ALIGN(4);
        _sdata = .;
        *(.data .data.*)
        . = ALIGN(4);
        _edata = .;
    } > DTCM

    .bss (NOLOAD) :
    {
        . = ALIGN(4);
        _sbss = .;
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
    } > DTCM

    .dmamem (NOLOAD) :
    {
        . = ALIGN(32);
        _sdmamem = .;
        *(.dmamem .dmamem.*)
        . = ALIGN(32);
        _edmamem = .;
    } > OCRAM2

    _estack = ORIGIN(DTCM) + LENGTH(DTCM);
    _extmem_start = ORIGIN(EXTMEM);
    _extmem_max_end = ORIGIN(EXTMEM) + LENGTH(EXTMEM);
}
