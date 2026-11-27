/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#ifndef OYK_LYRA_PICOCALC_AUDIO_HPP
#define OYK_LYRA_PICOCALC_AUDIO_HPP

#include "oyk_lyra_config.h"
#include "oyk_portable.hpp"

class LyraPicoCalcAudio {
public:
    LyraPicoCalcAudio();

    int init();
    void set_enabled(int en);
    int is_enabled() const { return enabled_; }
    void trigger_bel(oyk_u32 now_ms);
    void tick(oyk_u32 now_ms);

    int is_beeping() const { return active_; }
    oyk_u32 bel_count() const { return bel_count_; }

private:
    int enabled_;
    int active_;
    oyk_u32 stop_at_ms_;
    oyk_u32 bel_count_;
#if defined(OYK_LYRA_RTTHREAD) && OYK_LYRA_RTTHREAD
    void* pwm_dev_;
#endif
};

#endif /* OYK_LYRA_PICOCALC_AUDIO_HPP */
