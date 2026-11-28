/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */
#include "ti99_rs232.h"

#include <string.h>

int ti99_rs232_init(Ti99Rs232State *st, uint16_t baud)
{
    if (!st) {
        return -1;
    }
    memset(st, 0, sizeof(*st));
    st->baud = baud ? baud : 9600U;
    st->detected = 1U;
    return 0;
}

int ti99_rs232_write_byte(Ti99Rs232State *st, uint8_t b)
{
    uint16_t next;
    if (!st || !st->detected) {
        return -1;
    }
    next = (uint16_t)((st->rx_head + 1U) & 0xFFU);
    if (next == st->rx_tail) {
        return -1;
    }
    st->rx_ring[st->rx_head] = b;
    st->rx_head = next;
    return 0;
}

int ti99_rs232_read_byte(Ti99Rs232State *st, uint8_t *b_out)
{
    if (!st || !st->detected || !b_out) {
        return -1;
    }
    if (st->rx_head == st->rx_tail) {
        return 0;
    }
    *b_out = st->rx_ring[st->rx_tail];
    st->rx_tail = (uint16_t)((st->rx_tail + 1U) & 0xFFU);
    return 1;
}

void ti99_rs232_close(Ti99Rs232State *st)
{
    if (st) {
        st->detected = 0;
    }
}
