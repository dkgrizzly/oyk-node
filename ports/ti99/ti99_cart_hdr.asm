* SPDX-License-Identifier: GPL-3.0-or-later
* Copyright (C) 2026 KR4OYK - David Kuder
* FinalGROM99 8 KB Cartridge Bank Header for OYK Packet Node (TI-99/4A)

       DEF  SFIRST,SLAST,SLOAD
       AORG >6000

HEADER BYTE >AA,>01,>01,>00
       DATA >0000
       DATA PRG_LIST
       DATA >0000
       DATA >0000
       DATA >0000

PRG_LIST:
       DATA >0000
       DATA MAIN_ENTRY
       BYTE 15
       TEXT 'OYK PACKET NODE'
       EVEN

MAIN_ENTRY:
       LWPI >8300
       LIMI 0
       CLR  R12
* Bank switch trampoline enters C runtime linked at >A000 via SAMS / 32K RAM
       BL   @>A000
HALT_LP:
       JMP  HALT_LP
SFIRST EQU  $
SLAST  EQU  $
SLOAD  EQU  >6000
       END
