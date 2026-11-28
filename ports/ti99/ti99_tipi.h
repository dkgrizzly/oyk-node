/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */
#ifndef OYK_TI99_TIPI_H
#define OYK_TI99_TIPI_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Ti99TipiState {
    uint8_t detected;
    uint8_t connected;
    char endpoint[48];
    uint8_t mock_rx[330];
    size_t mock_rx_len;
} Ti99TipiState;

int ti99_tipi_probe(Ti99TipiState *st);
int ti99_tipi_open_udp(Ti99TipiState *st, const char *host, uint16_t port);
int ti99_tipi_send(Ti99TipiState *st, const uint8_t *data, size_t len);
int ti99_tipi_recv(Ti99TipiState *st, uint8_t *out, size_t cap);
void ti99_tipi_close(Ti99TipiState *st);

#ifdef __cplusplus
}
#endif

#endif /* OYK_TI99_TIPI_H */
