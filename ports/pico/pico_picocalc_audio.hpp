/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#ifndef OYK_PICO_PICOCALC_AUDIO_HPP
#define OYK_PICO_PICOCALC_AUDIO_HPP

#include "oyk_pico_config.h"
#include "oyk_portable.hpp"

class PicoCalcAudio {
public:
    PicoCalcAudio();

    int init();
    void trigger_bel(oyk_u32 now_ms);
    void tick(oyk_u32 now_ms);

    int bel_active() const { return active_; }
    oyk_u32 bel_count() const { return bel_count_; }

private:
    int active_;
    oyk_u32 stop_at_ms_;
    oyk_u32 bel_count_;
};

#endif /* OYK_PICO_PICOCALC_AUDIO_HPP */
