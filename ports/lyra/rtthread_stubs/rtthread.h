/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */

#ifndef OYK_LYRA_RTTHREAD_STUB_H
#define OYK_LYRA_RTTHREAD_STUB_H

/* Minimal RT-Thread SMP API declarations for offline ARMv7 cross-compile verification. */
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RT_NULL                  (0)
#define RT_EOK                   0
#define RT_DEVICE_FLAG_RDWR      0x003U
#define RT_DEVICE_FLAG_INT_RX    0x100U
#define RT_DEVICE_CTRL_CONFIG    0x03

typedef long                     rt_base_t;
typedef unsigned long            rt_ubase_t;
typedef long                     rt_ssize_t;
typedef unsigned long            rt_tick_t;
typedef unsigned int             rt_uint32_t;
typedef int                      rt_err_t;

struct rt_device {
    int dummy;
};
typedef struct rt_device*        rt_device_t;

struct rt_thread {
    int dummy;
};
typedef struct rt_thread*        rt_thread_t;

rt_device_t rt_device_find(const char* name);
rt_err_t rt_device_open(rt_device_t dev, unsigned short oflag);
rt_err_t rt_device_control(rt_device_t dev, int cmd, void* arg);
rt_ssize_t rt_device_read(rt_device_t dev, long pos, void* buffer, size_t size);
rt_ssize_t rt_device_write(rt_device_t dev, long pos, const void* buffer, size_t size);
rt_device_t rt_console_get_device(void);

rt_tick_t rt_tick_get_millisecond(void);
void rt_thread_mdelay(unsigned long ms);
void rt_hw_cpu_reset(void);
void rt_hw_cpu_shutdown(void);

rt_thread_t rt_thread_create(const char* name, void (*entry)(void*), void* parameter,
                             rt_uint32_t stack_size, unsigned char priority, rt_uint32_t tick);
rt_err_t rt_thread_startup(rt_thread_t thread);

#define INIT_APP_EXPORT(fn) \
    typedef int (*oyk_init_fn_##fn)(void); \
    static oyk_init_fn_##fn oyk_init_slot_##fn __attribute__((unused)) = (fn)

#define MSH_CMD_EXPORT_ALIAS(cmd, alias, desc) \
    static const char* oyk_msh_desc_##alias __attribute__((unused)) = (#desc)

#ifdef __cplusplus
}
#endif

#endif /* OYK_LYRA_RTTHREAD_STUB_H */
