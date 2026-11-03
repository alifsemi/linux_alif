// SPDX-License-Identifier: GPL-2.0-only
/*
 * Shared A32 shutdown guard and deferred orderly_poweroff().
 * MHU RX/ACK lives in drivers/rpmsg/rpmsg_arm_mhu_client.c (rxdb2/txdb2
 * on arm,client), the same endpoints as pthread_m55_he_mhu0_inloop.
 *
 * Copyright (C) 2026 Alif Semiconductor
 */
#include <linux/atomic.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/reboot.h>
#include <linux/workqueue.h>
#include <mach/a32_pm.h>

static atomic_t shutdown_in_progress = ATOMIC_INIT(0);

bool a32_shutdown_try_begin(void)
{
	if (system_state > SYSTEM_RUNNING)
		return false;
	return atomic_cmpxchg(&shutdown_in_progress, 0, 1) == 0;
}
EXPORT_SYMBOL_GPL(a32_shutdown_try_begin);

void a32_shutdown_mark(void)
{
	atomic_set(&shutdown_in_progress, 1);
}
EXPORT_SYMBOL_GPL(a32_shutdown_mark);

static void a32_m55_poweroff_work(struct work_struct *work)
{
	pr_emerg("a32-m55-shutdown: starting orderly_poweroff (M55 request)\n");
	orderly_poweroff(true);
}

static DECLARE_WORK(a32_m55_poweroff_wrk, a32_m55_poweroff_work);

void a32_m55_on_periph_off_req(void)
{
	if (system_state > SYSTEM_RUNNING) {
		pr_emerg("a32-m55-shutdown: shutdown already in flight, skip\n");
		return;
	}
	if (!a32_shutdown_try_begin()) {
		pr_emerg("a32-m55-shutdown: guard set, skip second orderly_poweroff\n");
		return;
	}
	schedule_work(&a32_m55_poweroff_wrk);
}
EXPORT_SYMBOL_GPL(a32_m55_on_periph_off_req);

static int a32_m55_shutdown_reboot(struct notifier_block *nb,
				   unsigned long mode, void *cmd)
{
	if (mode == SYS_POWER_OFF)
		a32_shutdown_mark();
	return NOTIFY_DONE;
}

static struct notifier_block a32_m55_reboot_nb = {
	.notifier_call = a32_m55_shutdown_reboot,
	.priority = INT_MAX - 1,
};

static int __init a32_m55_shutdown_init(void)
{
	return register_reboot_notifier(&a32_m55_reboot_nb);
}
late_initcall(a32_m55_shutdown_init);
