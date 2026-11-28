/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */
#ifndef OYK_TI99_RS232_H
#define OYK_TI99_RS232_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TI99_RS232_CRU_BASE     0x1300U

typedef struct Ti99Rs232State {
    uint8_t detected;
    uint16_t baud;
    uint8_t rx_ring[256];
    uint16_t rx_head;
    uint16_t rx_tail;
} Ti99Rs232State;

int ti99_rs232_init(Ti99Rs232State *st, uint16_t baud);
int ti99_rs232_write_byte(Ti99Rs232State *st, uint8_t b);
int ti99_rs232_read_byte(Ti99Rs232State *st, uint8_t *b_out);
void ti99_rs232_close(Ti99Rs232State *st);

#ifdef __cplusplus
}
#endif

#endif /* OYK_TI99_RS232_H */
