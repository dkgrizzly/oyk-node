/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#include "board_lyra.hpp"

#include <string.h>

static const LyraBoardProfile s_lyra_profiles[LYRA_TOTAL_BOARD_PROFILES] = {
    {
        "lyra",
        "Luckfox Lyra (RK3506G2 128M)",
        LYRA_SOC_RK3506G2,
        128U, 0U,
        LYRA_NET_USB_RNDIS_PPP,
        0U, 2U, 0U,
        0U, 0U, 0U, 0U, 0U
    },
    {
        "lyra_b",
        "Luckfox Lyra B (RK3506G2 128M+NAND)",
        LYRA_SOC_RK3506G2,
        128U, 256U,
        LYRA_NET_USB_RNDIS_PPP,
        0U, 2U, 0U,
        0U, 0U, 0U, 0U, 0U
    },
    {
        "lyra_plus",
        "Luckfox Lyra Plus (RK3506G2 RMII)",
        LYRA_SOC_RK3506G2,
        128U, 256U,
        LYRA_NET_RMII_ETH,
        0U, 2U, 0U,
        0U, 0U, 0U, 0U, 0U
    },
    {
        "lyra_zero_w",
        "Luckfox Lyra Zero W (RK3506B WiFi6)",
        LYRA_SOC_RK3506B,
        512U, 256U,
        LYRA_NET_WIFI6_BLE,
        0U, 2U, 0U,
        0U, 0U, 0U, 0U, 0U
    },
    {
        "picocalc_lyra",
        "ClockworkPi PicoCalc (Luckfox Lyra)",
        LYRA_SOC_RK3506G2,
        128U, 0U,
        LYRA_NET_USB_RNDIS_PPP,
        1U, 1U, 1U,
        OYK_LYRA_LCD_CS_PIN, OYK_LYRA_LCD_DC_PIN, OYK_LYRA_LCD_RST_PIN,
        OYK_LYRA_KBD_SCL_PIN, OYK_LYRA_KBD_SDA_PIN
    },
    {
        "picocalc_lyra_b",
        "ClockworkPi PicoCalc (Lyra B)",
        LYRA_SOC_RK3506G2,
        128U, 256U,
        LYRA_NET_USB_RNDIS_PPP,
        1U, 1U, 1U,
        OYK_LYRA_LCD_CS_PIN, OYK_LYRA_LCD_DC_PIN, OYK_LYRA_LCD_RST_PIN,
        OYK_LYRA_KBD_SCL_PIN, OYK_LYRA_KBD_SDA_PIN
    },
    {
        "picocalc_lyra_plus",
        "ClockworkPi PicoCalc (Lyra Plus)",
        LYRA_SOC_RK3506G2,
        128U, 256U,
        LYRA_NET_RMII_ETH,
        1U, 1U, 1U,
        OYK_LYRA_LCD_CS_PIN, OYK_LYRA_LCD_DC_PIN, OYK_LYRA_LCD_RST_PIN,
        OYK_LYRA_KBD_SCL_PIN, OYK_LYRA_KBD_SDA_PIN
    }
};

static const LyraUartMap s_lyra_uarts[OYK_LYRA_UART_COUNT] = {
    { 0, 1, 0xFF0B0000UL, OYK_LYRA_SER0_UART_DEV, "ser0" },
    { 1, 2, 0xFF0C0000UL, OYK_LYRA_SER1_UART_DEV, "ser1" }
};

static const LyraBoardProfile* s_active_lyra_profile = NULL;

int lyra_board_profile_count() {
    return LYRA_TOTAL_BOARD_PROFILES;
}

const LyraBoardProfile* lyra_board_profile_at(int idx) {
    if (idx < 0 || idx >= LYRA_TOTAL_BOARD_PROFILES) return NULL;
    return &s_lyra_profiles[idx];
}

const LyraBoardProfile* lyra_board_profile_by_id(const char* board_id) {
    if (!board_id || !board_id[0]) return NULL;
    for (int i = 0; i < LYRA_TOTAL_BOARD_PROFILES; ++i) {
        if (strcmp(s_lyra_profiles[i].id, board_id) == 0) {
            return &s_lyra_profiles[i];
        }
    }
    return NULL;
}

const LyraBoardProfile* lyra_board_current_profile() {
    if (s_active_lyra_profile) return s_active_lyra_profile;
    const LyraBoardProfile* p = lyra_board_profile_by_id(OYK_LYRA_BOARD_ID);
    return p ? p : &s_lyra_profiles[0];
}

void lyra_board_set_sim_profile(const char* board_id) {
    const LyraBoardProfile* p = lyra_board_profile_by_id(board_id);
    if (p) s_active_lyra_profile = p;
}

const LyraUartMap* lyra_uart_table() {
    return s_lyra_uarts;
}

const LyraUartMap* lyra_uart_info(int logical_port) {
    const LyraBoardProfile* prof = lyra_board_current_profile();
    int limit = prof ? static_cast<int>(prof->uart_count) : OYK_LYRA_UART_COUNT;
    if (limit > OYK_LYRA_UART_COUNT) limit = OYK_LYRA_UART_COUNT;
    if (logical_port < 0 || logical_port >= limit) return NULL;
    return &s_lyra_uarts[logical_port];
}

#if defined(OYK_LYRA_RTTHREAD) && OYK_LYRA_RTTHREAD

#include <rtthread.h>
#include <rtdevice.h>
#include "hal_base.h"
#include "hal_pinctrl.h"

static rt_device_t s_rt_uarts[OYK_LYRA_UART_COUNT] = { RT_NULL, RT_NULL };

void lyra_board_init_pinmux() {
    const LyraBoardProfile* prof = lyra_board_current_profile();
    if (prof->has_picocalc_hw) {
        HAL_PINCTRL_SetRMIO(GPIO_BANK0, GPIO_PIN_A2, RMIO_GPIO0_A2);
        HAL_PINCTRL_SetRMIO(GPIO_BANK0, GPIO_PIN_A3, RMIO_GPIO0_A3);
        HAL_PINCTRL_SetRMIO(GPIO_BANK0, GPIO_PIN_A4, RMIO_GPIO0_A4);
        HAL_PINCTRL_SetRMIO(GPIO_BANK0, GPIO_PIN_B2, RMIO_I2C2_SCL);
        HAL_PINCTRL_SetRMIO(GPIO_BANK0, GPIO_PIN_B3, RMIO_I2C2_SDA);
    }
    HAL_PINCTRL_SetRMIO(GPIO_BANK0, GPIO_PIN_B4, RMIO_UART1_TX);
    HAL_PINCTRL_SetRMIO(GPIO_BANK0, GPIO_PIN_B5, RMIO_UART1_RX);
    if (prof->uart_count > 1U) {
        HAL_PINCTRL_SetRMIO(GPIO_BANK0, GPIO_PIN_B6, RMIO_UART2_TX);
        HAL_PINCTRL_SetRMIO(GPIO_BANK0, GPIO_PIN_B7, RMIO_UART2_RX);
    }
}

void lyra_board_init() {
    lyra_board_init_pinmux();
}

void lyra_board_read_mac(oyk_u8 mac_out[6]) {
    if (!mac_out) return;
    uint32_t chip_uid = HAL_GetChipID();
    mac_out[0] = 0x02U;
    mac_out[1] = 0x52U;
    mac_out[2] = static_cast<oyk_u8>((chip_uid >> 24) & 0xFFU);
    mac_out[3] = static_cast<oyk_u8>((chip_uid >> 16) & 0xFFU);
    mac_out[4] = static_cast<oyk_u8>((chip_uid >> 8) & 0xFFU);
    mac_out[5] = static_cast<oyk_u8>(chip_uid & 0xFFU);
}

oyk_u32 lyra_uptime_ms() {
    return static_cast<oyk_u32>(rt_tick_get_millisecond());
}

void lyra_board_reboot() {
    rt_hw_cpu_reset();
}

void lyra_board_poweroff(oyk_u8 delay_sec) {
    const LyraBoardProfile* prof = lyra_board_current_profile();
    if (prof->has_picocalc_hw) {
        rt_device_t bus = rt_device_find(OYK_LYRA_KBD_I2C_DEV);
        if (bus) {
            oyk_u8 payload[2];
            payload[0] = 0x8EU; /* REG_ID_OFF (0x0E) | REG_WRITE (0x80) */
            payload[1] = (delay_sec > 63U) ? 63U : delay_sec;
            struct rt_i2c_msg msg;
            msg.addr = OYK_LYRA_KBD_I2C_ADDR;
            msg.flags = RT_I2C_WR;
            msg.len = 2U;
            msg.buf = payload;
            rt_i2c_transfer(reinterpret_cast<struct rt_i2c_bus_device*>(bus), &msg, 1);
        }
    }
    rt_hw_cpu_shutdown();
}

int lyra_board_sim_reboot_count() { return 0; }
int lyra_board_sim_poweroff_count() { return 0; }

int lyra_uart_init(int logical_port, const LyraUartConfig* cfg) {
    const LyraUartMap* m = lyra_uart_info(logical_port);
    if (!m || !cfg || cfg->baudrate == 0UL) return 0;
    rt_device_t dev = rt_device_find(m->rt_dev_name);
    if (!dev) return 0;
    struct serial_configure scfg = RT_SERIAL_CONFIG_DEFAULT;
    scfg.baud_rate = cfg->baudrate;
    rt_device_control(dev, RT_DEVICE_CTRL_CONFIG, &scfg);
    if (rt_device_open(dev, RT_DEVICE_FLAG_RDWR | RT_DEVICE_FLAG_INT_RX) != RT_EOK) {
        return 0;
    }
    s_rt_uarts[logical_port] = dev;
    return 1;
}

size_t lyra_uart_write(int logical_port, const oyk_u8* data, size_t len) {
    if (logical_port < 0 || logical_port >= OYK_LYRA_UART_COUNT || !data || len == 0) return 0;
    rt_device_t dev = s_rt_uarts[logical_port];
    if (!dev) return 0;
    rt_ssize_t written = rt_device_write(dev, 0, data, len);
    return (written > 0) ? static_cast<size_t>(written) : 0U;
}

size_t lyra_uart_read(int logical_port, oyk_u8* out, size_t max_len) {
    if (logical_port < 0 || logical_port >= OYK_LYRA_UART_COUNT || !out || max_len == 0) return 0;
    rt_device_t dev = s_rt_uarts[logical_port];
    if (!dev) return 0;
    rt_ssize_t got = rt_device_read(dev, 0, out, max_len);
    return (got > 0) ? static_cast<size_t>(got) : 0U;
}

void lyra_uart_set_sim_tx_hook(LyraUartSimTxFn /*cb*/, void* /*ctx*/) {}
void lyra_uart_sim_inject_rx(int /*logical_port*/, const oyk_u8* /*data*/, size_t /*len*/) {}

#else

/* Host test simulation */
static int s_sim_reboots = 0;
static int s_sim_poweroffs = 0;
static LyraUartSimTxFn s_sim_tx_cb = NULL;
static void* s_sim_tx_ctx = NULL;
static oyk_u8 s_sim_rx[OYK_LYRA_UART_COUNT][512];
static size_t s_sim_rx_len[OYK_LYRA_UART_COUNT] = {0, 0};

void lyra_board_init_pinmux() {}

void lyra_board_init() {}

void lyra_board_read_mac(oyk_u8 mac_out[6]) {
    if (!mac_out) return;
    static const oyk_u8 kDefaultMac[6] = {0x02U, 0x52U, 0x35U, 0x06U, 0x4CU, 0x59U};
    memcpy(mac_out, kDefaultMac, 6);
}

oyk_u32 lyra_uptime_ms() { return 0UL; }

void lyra_board_reboot() { ++s_sim_reboots; }

void lyra_board_poweroff(oyk_u8 /*delay_sec*/) { ++s_sim_poweroffs; }

int lyra_board_sim_reboot_count() { return s_sim_reboots; }
int lyra_board_sim_poweroff_count() { return s_sim_poweroffs; }

int lyra_uart_init(int logical_port, const LyraUartConfig* cfg) {
    const LyraUartMap* m = lyra_uart_info(logical_port);
    if (!m || !cfg || cfg->baudrate == 0UL) return 0;
    s_sim_rx_len[logical_port] = 0;
    return 1;
}

size_t lyra_uart_write(int logical_port, const oyk_u8* data, size_t len) {
    if (!lyra_uart_info(logical_port) || !data || len == 0) return 0;
    if (s_sim_tx_cb) s_sim_tx_cb(logical_port, data, len, s_sim_tx_ctx);
    return len;
}

size_t lyra_uart_read(int logical_port, oyk_u8* out, size_t max_len) {
    if (!lyra_uart_info(logical_port) || !out || max_len == 0) return 0;
    size_t avail = s_sim_rx_len[logical_port];
    if (avail == 0) return 0;
    size_t n = (avail < max_len) ? avail : max_len;
    memcpy(out, s_sim_rx[logical_port], n);
    if (n < avail) {
        memmove(s_sim_rx[logical_port], s_sim_rx[logical_port] + n, avail - n);
    }
    s_sim_rx_len[logical_port] = avail - n;
    return n;
}

void lyra_uart_set_sim_tx_hook(LyraUartSimTxFn cb, void* ctx) {
    s_sim_tx_cb = cb;
    s_sim_tx_ctx = ctx;
}

void lyra_uart_sim_inject_rx(int logical_port, const oyk_u8* data, size_t len) {
    if (logical_port < 0 || logical_port >= OYK_LYRA_UART_COUNT || !data || len == 0) return;
    size_t room = sizeof(s_sim_rx[logical_port]) - s_sim_rx_len[logical_port];
    size_t n = (len < room) ? len : room;
    if (n > 0) {
        memcpy(s_sim_rx[logical_port] + s_sim_rx_len[logical_port], data, n);
        s_sim_rx_len[logical_port] += n;
    }
}

#endif
