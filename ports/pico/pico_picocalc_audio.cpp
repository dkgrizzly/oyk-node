/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#include "pico_picocalc_audio.hpp"

#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
#include "pico/stdlib.h"
#include "hardware/pwm.h"
#include "hardware/gpio.h"
#include "hardware/clocks.h"
#endif

PicoCalcAudio::PicoCalcAudio()
    : active_(0), stop_at_ms_(0UL), bel_count_(0UL) {}

int PicoCalcAudio::init() {
    active_ = 0;
    stop_at_ms_ = 0UL;
    bel_count_ = 0UL;
#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
    gpio_set_function(OYK_PICOCALC_AUDIO_L_PIN, GPIO_FUNC_PWM);
    gpio_set_function(OYK_PICOCALC_AUDIO_R_PIN, GPIO_FUNC_PWM);
    const uint sL = pwm_gpio_to_slice_num(OYK_PICOCALC_AUDIO_L_PIN);
    const uint sR = pwm_gpio_to_slice_num(OYK_PICOCALC_AUDIO_R_PIN);
    const uint32_t clk = clock_get_hz(clk_sys);
    const uint16_t wrap = 1000U;
    float div = static_cast<float>(clk) / (880.0f * static_cast<float>(wrap));
    if (div < 1.0f) div = 1.0f;
    pwm_set_clkdiv(sL, div);
    pwm_set_wrap(sL, wrap);
    pwm_set_gpio_level(OYK_PICOCALC_AUDIO_L_PIN, 0U);
    pwm_set_enabled(sL, true);
    if (sR != sL) {
        pwm_set_clkdiv(sR, div);
        pwm_set_wrap(sR, wrap);
        pwm_set_enabled(sR, true);
    }
    pwm_set_gpio_level(OYK_PICOCALC_AUDIO_R_PIN, 0U);
#endif
    return 1;
}

void PicoCalcAudio::trigger_bel(oyk_u32 now_ms) {
    active_ = 1;
    stop_at_ms_ = now_ms + 60UL;
    bel_count_++;
#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
    pwm_set_gpio_level(OYK_PICOCALC_AUDIO_L_PIN, 250U);
    pwm_set_gpio_level(OYK_PICOCALC_AUDIO_R_PIN, 250U);
#endif
}

void PicoCalcAudio::tick(oyk_u32 now_ms) {
    if (!active_) return;
    if (static_cast<oyk_u32>(now_ms - stop_at_ms_) < 0x80000000UL) {
        active_ = 0;
#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
        pwm_set_gpio_level(OYK_PICOCALC_AUDIO_L_PIN, 0U);
        pwm_set_gpio_level(OYK_PICOCALC_AUDIO_R_PIN, 0U);
#endif
    }
}
