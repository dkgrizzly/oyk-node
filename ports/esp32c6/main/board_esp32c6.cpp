/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#include "board_esp32c6.hpp"
#include <string.h>

#ifdef ESP_PLATFORM
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "driver/uart.h"
#include "esp_system.h"
#include "esp_timer.h"
#endif

static const Esp32C6BoardProfile s_c6l_profile = {
    "m5stack_unit_c6l",
    "M5Stack Unit C6L (ESP32-C6 + SX1262)",
    16UL * 1024UL * 1024UL,
    512UL * 1024UL,
    OYK_C6L_SPI_SCK_PIN,
    OYK_C6L_SPI_MOSI_PIN,
    OYK_C6L_SPI_MISO_PIN,
    OYK_C6L_LORA_CS_PIN,
    OYK_C6L_LORA_DIO1_PIN,
    OYK_C6L_LORA_BUSY_PIN,
    OYK_C6L_OLED_CS_PIN,
    OYK_C6L_OLED_DC_PIN,
    OYK_C6L_OLED_RST_PIN,
    OYK_C6L_I2C_SDA_PIN,
    OYK_C6L_I2C_SCL_PIN,
    OYK_C6L_PI4IO_INT_PIN,
    OYK_C6L_PI4IO_ADDR,
    OYK_C6L_RGB_LED_PIN,
    OYK_C6L_BUZZER_PIN,
    OYK_C6L_GROVE_TX_PIN,
    OYK_C6L_GROVE_RX_PIN
};

static const Esp32C6UartMap s_c6l_uarts[OYK_ESP32C6_UART_COUNT] = {
    { 0, 1, OYK_C6L_GROVE_TX_PIN, OYK_C6L_GROVE_RX_PIN, "ser0" }
};

static oyk_u8 s_pi4io_out = 0;
static oyk_u8 s_pi4io_in = OYK_C6L_PI4IO_BTN_BIT; /* Active-low button released = 1 */
static int s_prev_btn_pressed = 0;
static oyk_u32 s_last_btn_ms = 0;
static Esp32C6LedState s_led_state = C6L_LED_OFF;
static oyk_u8 s_led_r = 0;
static oyk_u8 s_led_g = 0;
static oyk_u8 s_led_b = 0;
static oyk_u16 s_last_buz_freq = 0;
static oyk_u16 s_last_buz_ms = 0;
static int s_sim_bootloader_cnt = 0;

#ifdef ESP_PLATFORM
static void pi4io_write_reg(oyk_u8 reg, oyk_u8 val) {
    oyk_u8 buf[2];
    buf[0] = reg;
    buf[1] = val;
    i2c_master_write_to_device(I2C_NUM_0, OYK_C6L_PI4IO_ADDR, buf, 2, pdMS_TO_TICKS(20));
}

static oyk_u8 pi4io_read_reg(oyk_u8 reg, oyk_u8 def_val) {
    oyk_u8 v = def_val;
    i2c_master_write_read_device(I2C_NUM_0, OYK_C6L_PI4IO_ADDR, &reg, 1, &v, 1, pdMS_TO_TICKS(20));
    return v;
}
#endif

const Esp32C6BoardProfile* esp32c6_current_board_profile() {
    return &s_c6l_profile;
}

const Esp32C6UartMap* esp32c6_uart_info(int logical_port) {
    if (logical_port < 0 || logical_port >= OYK_ESP32C6_UART_COUNT) return NULL;
    return &s_c6l_uarts[logical_port];
}

void esp32c6_board_init() {
    s_pi4io_out = OYK_C6L_PI4IO_NRST_BIT;
    s_pi4io_in = OYK_C6L_PI4IO_BTN_BIT;
    s_prev_btn_pressed = 0;
    s_last_btn_ms = 0;
#ifdef ESP_PLATFORM
    i2c_config_t icfg;
    memset(&icfg, 0, sizeof(icfg));
    icfg.mode = I2C_MODE_MASTER;
    icfg.sda_io_num = static_cast<int>(OYK_C6L_I2C_SDA_PIN);
    icfg.scl_io_num = static_cast<int>(OYK_C6L_I2C_SCL_PIN);
    icfg.sda_pullup_en = GPIO_PULLUP_ENABLE;
    icfg.scl_pullup_en = GPIO_PULLUP_ENABLE;
    icfg.master.clk_speed = 400000;
    i2c_param_config(I2C_NUM_0, &icfg);
    i2c_driver_install(I2C_NUM_0, I2C_MODE_MASTER, 0, 0, 0);

    /* Configure PI4IOE5V6408: P5,P6,P7 outputs; P0 User Button input with pull-up */
    pi4io_write_reg(0x03U, 0xE0U); /* IO direction: P5..P7 outputs */
    pi4io_write_reg(0x07U, 0x1FU); /* Output Hi-Z off for outputs */
    pi4io_write_reg(0x0BU, OYK_C6L_PI4IO_BTN_BIT); /* Pull enable P0 */
    pi4io_write_reg(0x0DU, OYK_C6L_PI4IO_BTN_BIT); /* Pull-up on P0 */
    pi4io_write_reg(0x05U, s_pi4io_out);

    spi_bus_config_t buscfg;
    memset(&buscfg, 0, sizeof(buscfg));
    buscfg.mosi_io_num = static_cast<int>(OYK_C6L_SPI_MOSI_PIN);
    buscfg.miso_io_num = static_cast<int>(OYK_C6L_SPI_MISO_PIN);
    buscfg.sclk_io_num = static_cast<int>(OYK_C6L_SPI_SCK_PIN);
    buscfg.quadwp_io_num = -1;
    buscfg.quadhd_io_num = -1;
    spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);

    uart_config_t ucfg;
    memset(&ucfg, 0, sizeof(ucfg));
    ucfg.baud_rate = 115200;
    ucfg.data_bits = UART_DATA_8_BITS;
    ucfg.parity = UART_PARITY_DISABLE;
    ucfg.stop_bits = UART_STOP_BITS_1;
    ucfg.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    uart_driver_install(UART_NUM_1, 512, 512, 0, NULL, 0);
    uart_param_config(UART_NUM_1, &ucfg);
    uart_set_pin(UART_NUM_1, OYK_C6L_GROVE_TX_PIN, OYK_C6L_GROVE_RX_PIN, -1, -1);

    ledc_timer_config_t ltm;
    memset(&ltm, 0, sizeof(ltm));
    ltm.speed_mode = LEDC_LOW_SPEED_MODE;
    ltm.duty_resolution = LEDC_TIMER_10_BIT;
    ltm.timer_num = LEDC_TIMER_0;
    ltm.freq_hz = 2000;
    ltm.clk_cfg = LEDC_AUTO_CLK;
    ledc_timer_config(&ltm);

    ledc_channel_config_t lch;
    memset(&lch, 0, sizeof(lch));
    lch.gpio_num = static_cast<int>(OYK_C6L_BUZZER_PIN);
    lch.speed_mode = LEDC_LOW_SPEED_MODE;
    lch.channel = LEDC_CHANNEL_0;
    lch.timer_sel = LEDC_TIMER_0;
    lch.duty = 0;
    ledc_channel_config(&lch);
#endif
    esp32c6_board_set_rgb_led(C6L_LED_IDLE, 0U, 24U, 48U);
}

void esp32c6_board_set_lora_rf_path(int lna_en, int ant_sw, int nrst_high) {
    oyk_u8 v = 0;
    if (lna_en) v |= OYK_C6L_PI4IO_LNA_BIT;
    if (ant_sw) v |= OYK_C6L_PI4IO_ANT_BIT;
    if (nrst_high) v |= OYK_C6L_PI4IO_NRST_BIT;
    s_pi4io_out = v;
#ifdef ESP_PLATFORM
    pi4io_write_reg(0x05U, v);
#endif
}

oyk_u8 esp32c6_board_pi4io_out_reg() { return s_pi4io_out; }
oyk_u8 esp32c6_board_pi4io_in_reg() { return s_pi4io_in; }

void esp32c6_board_sim_set_button(int pressed) {
    if (pressed) s_pi4io_in &= static_cast<oyk_u8>(~OYK_C6L_PI4IO_BTN_BIT);
    else s_pi4io_in |= OYK_C6L_PI4IO_BTN_BIT;
}

int esp32c6_board_poll_button_press(oyk_u32 now_ms) {
#ifdef ESP_PLATFORM
    s_pi4io_in = pi4io_read_reg(0x0FU, OYK_C6L_PI4IO_BTN_BIT);
#endif
    int pressed = ((s_pi4io_in & OYK_C6L_PI4IO_BTN_BIT) == 0U) ? 1 : 0;
    int rising_edge = 0;
    if (pressed && !s_prev_btn_pressed && (now_ms - s_last_btn_ms) >= 40UL) {
        rising_edge = 1;
        s_last_btn_ms = now_ms;
    }
    s_prev_btn_pressed = pressed;
    return rising_edge;
}

void esp32c6_board_set_rgb_led(Esp32C6LedState state, oyk_u8 r, oyk_u8 g, oyk_u8 b) {
    s_led_state = state;
    s_led_r = r;
    s_led_g = g;
    s_led_b = b;
}

Esp32C6LedState esp32c6_board_rgb_state() { return s_led_state; }

void esp32c6_board_rgb_color(oyk_u8* r, oyk_u8* g, oyk_u8* b) {
    if (r) *r = s_led_r;
    if (g) *g = s_led_g;
    if (b) *b = s_led_b;
}

void esp32c6_board_buzzer_beep(oyk_u16 freq_hz, oyk_u16 duration_ms) {
    s_last_buz_freq = freq_hz;
    s_last_buz_ms = duration_ms;
#ifdef ESP_PLATFORM
    if (freq_hz > 0U && duration_ms > 0U) {
        ledc_set_freq(LEDC_LOW_SPEED_MODE, LEDC_TIMER_0, freq_hz);
        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 256);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
        esp_rom_delay_us(static_cast<uint32_t>(duration_ms > 50U ? 50U : duration_ms) * 1000UL);
        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    }
#endif
}

oyk_u16 esp32c6_board_last_buzzer_freq() { return s_last_buz_freq; }
oyk_u16 esp32c6_board_last_buzzer_ms() { return s_last_buz_ms; }

void esp32c6_board_set_uart_baud(int logical_port, oyk_u32 baud) {
#ifdef ESP_PLATFORM
    if (logical_port == 0 && baud >= 1200UL) {
        uart_set_baudrate(UART_NUM_1, static_cast<uint32_t>(baud));
    }
#else
    (void)logical_port;
    (void)baud;
#endif
}

int esp32c6_board_uart_write(int logical_port, const oyk_u8* data, size_t len) {
#ifdef ESP_PLATFORM
    if (logical_port == 0 && data && len > 0) {
        return uart_write_bytes(UART_NUM_1, data, len);
    }
    return 0;
#else
    (void)logical_port;
    (void)data;
    return static_cast<int>(len);
#endif
}

size_t esp32c6_board_uart_read(int logical_port, oyk_u8* out, size_t max_len) {
#ifdef ESP_PLATFORM
    if (logical_port == 0 && out && max_len > 0) {
        int n = uart_read_bytes(UART_NUM_1, out, static_cast<uint32_t>(max_len), 0);
        return (n > 0) ? static_cast<size_t>(n) : 0;
    }
    return 0;
#else
    (void)logical_port;
    (void)out;
    (void)max_len;
    return 0;
#endif
}

oyk_u32 esp32c6_board_millis() {
#ifdef ESP_PLATFORM
    return static_cast<oyk_u32>(esp_timer_get_time() / 1000LL);
#else
    return 0UL;
#endif
}

void esp32c6_board_enter_bootloader() {
    ++s_sim_bootloader_cnt;
#ifdef ESP_PLATFORM
    esp_restart();
#endif
}

int esp32c6_board_sim_bootloader_count() {
    return s_sim_bootloader_cnt;
}
