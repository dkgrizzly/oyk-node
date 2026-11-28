/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */
#include "ti99_tipi.h"

#include <stdio.h>
#include <string.h>

int ti99_tipi_probe(Ti99TipiState *st)
{
    if (!st) {
        return -1;
    }
    memset(st, 0, sizeof(*st));
#ifdef __TMS9900__
    /* TIPI DSR typically resides at CRU >1000..>1F00 when PI.TCP is registered */
    st->detected = 1U;
#else
    st->detected = 1U;
#endif
    return 0;
}

int ti99_tipi_open_udp(Ti99TipiState *st, const char *host, uint16_t port)
{
    if (!st || !st->detected || !host) {
        return -1;
    }
    snprintf(st->endpoint, sizeof(st->endpoint), "PI.UDP=%s:%u", host, (unsigned)port);
    st->connected = 1U;
    return 0;
}

int ti99_tipi_send(Ti99TipiState *st, const uint8_t *data, size_t len)
{
    if (!st || !st->connected || !data || len == 0 || len > sizeof(st->mock_rx)) {
        return -1;
    }
    memcpy(st->mock_rx, data, len);
    st->mock_rx_len = len;
    return (int)len;
}

int ti99_tipi_recv(Ti99TipiState *st, uint8_t *out, size_t cap)
{
    size_t n;
    if (!st || !st->connected || !out) {
        return -1;
    }
    if (st->mock_rx_len == 0) {
        return 0;
    }
    n = st->mock_rx_len;
    if (n > cap) {
        n = cap;
    }
    memcpy(out, st->mock_rx, n);
    st->mock_rx_len = 0;
    return (int)n;
}

void ti99_tipi_close(Ti99TipiState *st)
{
    if (st) {
        st->connected = 0;
        st->mock_rx_len = 0;
    }
}
