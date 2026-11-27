/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#include "lyra_picocalc_audio.hpp"

#if defined(OYK_LYRA_RTTHREAD) && OYK_LYRA_RTTHREAD
#include <rtthread.h>
#include <rtdevice.h>
#endif

LyraPicoCalcAudio::LyraPicoCalcAudio()
    : enabled_(OYK_LYRA_ENABLE_AUDIO ? 1 : 0),
      active_(0), stop_at_ms_(0UL), bel_count_(0UL)
#if defined(OYK_LYRA_RTTHREAD) && OYK_LYRA_RTTHREAD
      , pwm_dev_(NULL)
#endif
{}

int LyraPicoCalcAudio::init() {
    enabled_ = OYK_LYRA_ENABLE_AUDIO ? 1 : 0;
    active_ = 0;
    stop_at_ms_ = 0UL;
    bel_count_ = 0UL;
#if defined(OYK_LYRA_RTTHREAD) && OYK_LYRA_RTTHREAD
    pwm_dev_ = enabled_ ? rt_device_find(OYK_LYRA_AUDIO_PWM_DEV) : NULL;
    if (pwm_dev_) {
        struct rt_device_pwm* pwm = reinterpret_cast<struct rt_device_pwm*>(pwm_dev_);
        rt_pwm_set(pwm, 0, 1136364UL, 0UL); /* 880 Hz period in ns, 0% duty */
        rt_pwm_disable(pwm, 0);
    }
#endif
    return 1;
}

void LyraPicoCalcAudio::set_enabled(int en) {
    enabled_ = en ? 1 : 0;
    if (!enabled_ && active_) {
        active_ = 0;
#if defined(OYK_LYRA_RTTHREAD) && OYK_LYRA_RTTHREAD
        if (pwm_dev_) {
            rt_pwm_disable(reinterpret_cast<struct rt_device_pwm*>(pwm_dev_), 0);
        }
#endif
    }
}

void LyraPicoCalcAudio::trigger_bel(oyk_u32 now_ms) {
    if (!enabled_) return;
    active_ = 1;
    stop_at_ms_ = now_ms + 60UL;
    bel_count_++;
#if defined(OYK_LYRA_RTTHREAD) && OYK_LYRA_RTTHREAD
    if (pwm_dev_) {
        struct rt_device_pwm* pwm = reinterpret_cast<struct rt_device_pwm*>(pwm_dev_);
        rt_pwm_set(pwm, 0, 1136364UL, 284091UL); /* 880 Hz, 25% duty */
        rt_pwm_enable(pwm, 0);
    }
#endif
}

void LyraPicoCalcAudio::tick(oyk_u32 now_ms) {
    if (!active_) return;
    if (static_cast<oyk_u32>(now_ms - stop_at_ms_) < 0x80000000UL) {
        active_ = 0;
#if defined(OYK_LYRA_RTTHREAD) && OYK_LYRA_RTTHREAD
        if (pwm_dev_) {
            rt_pwm_disable(reinterpret_cast<struct rt_device_pwm*>(pwm_dev_), 0);
        }
#endif
    }
}
