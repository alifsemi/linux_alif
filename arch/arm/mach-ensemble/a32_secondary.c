// SPDX-License-Identifier: GPL-2.0-only
/*
 * Offline A32 secondary cores on power-off (same path as
 * echo 0 > /sys/devices/system/cpu/cpuN/online).
 *
 * Copyright (C) 2026 Alif Semiconductor
 */
#include <linux/cpu.h>
#include <linux/cpumask.h>
#include <linux/init.h>
#include <linux/reboot.h>
#include <linux/sched.h>
#include <mach/a32_pm.h>

#ifdef CONFIG_HOTPLUG_CPU
/*
 * Must run in a reboot notifier, before device_shutdown(). Calling
 * cpu_down() from machine_power_off() is too late (-EBUSY) and
 * smp_shutdown_nonboot_cpus() BUG_ONs if a secondary stays online.
 *
 * Pin to the reboot/boot CPU first: poweroff often runs on CPU1, and
 * skipping smp_processor_id() would try (and fail with -EPERM) to
 * offline CPU0 instead of CPU1.
 */
static int a32_poweroff_offline_secondaries(struct notifier_block *nb,
					    unsigned long mode, void *cmd)
{
	unsigned int primary, cpu;
	int err;

	if (mode != SYS_POWER_OFF)
		return NOTIFY_DONE;

	a32_shutdown_mark();

	primary = reboot_cpu;
	if (!cpu_online(primary))
		primary = 0;
	if (!cpu_online(primary))
		primary = cpumask_first(cpu_online_mask);

	if (smp_processor_id() != primary) {
		err = set_cpus_allowed_ptr(current, cpumask_of(primary));
		if (err)
			pr_emerg("Power-off: migrate to CPU%u failed: %d\n",
				 primary, err);
	}

	pr_emerg("Power-off: on CPU%u, keeping CPU%u\n",
		 smp_processor_id(), primary);

	for_each_online_cpu(cpu) {
		if (cpu == primary)
			continue;
		pr_emerg("Power-off: offlining CPU%u (cpu/online path)\n", cpu);
		err = remove_cpu(cpu);
		if (err)
			pr_emerg("Power-off: failed to offline CPU%u: %d\n",
				 cpu, err);
	}
	return NOTIFY_OK;
}

static struct notifier_block a32_poweroff_nb = {
	.notifier_call = a32_poweroff_offline_secondaries,
	.priority = INT_MAX,
};

static int __init a32_secondary_init(void)
{
	return register_reboot_notifier(&a32_poweroff_nb);
}
late_initcall(a32_secondary_init);
#endif
