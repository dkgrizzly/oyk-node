/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 KR4OYK - David Kuder */
#ifndef OYK_VMS_COMPAT_H
#define OYK_VMS_COMPAT_H

#if defined(__VAX)
#define OYK_VMS_ARCH_NAME "OpenVMS VAX"
#elif defined(__alpha) || defined(__ALPHA)
#define OYK_VMS_ARCH_NAME "OpenVMS Alpha"
#elif defined(__ia64) || defined(__IA64)
#define OYK_VMS_ARCH_NAME "OpenVMS I64"
#elif defined(__x86_64) || defined(__x86_64__)
#define OYK_VMS_ARCH_NAME "OpenVMS x86_64"
#else
#define OYK_VMS_ARCH_NAME "OpenVMS Host"
#endif

#if defined(__VMS)
#include <descrip.h>
#include <ssdef.h>
#include <starlet.h>
#include <smg$routines.h>
#endif

#endif /* OYK_VMS_COMPAT_H */
