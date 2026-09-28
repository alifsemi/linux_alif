/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef MACH_A32_PM_H
#define MACH_A32_PM_H

#include <linux/types.h>

/*
 * Shared shutdown guard: first of (M55 MHU request, local poweroff)
 * claims it. A second orderly_poweroff() is skipped.
 */
bool a32_shutdown_try_begin(void);
void a32_shutdown_mark(void);
void a32_m55_on_periph_off_req(void);

#endif
