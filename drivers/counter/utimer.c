// SPDX-License-Identifier: GPL-2.0
/*
 * Counter driver for the Alif Semiconductor utimer IP
 * Copyright (C) 2021-2025 Alif Semiconductor
 */
#include <linux/bitops.h>
#include <linux/counter.h>
#include <linux/math64.h>
#include <linux/clockchips.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/ktime.h>
#include <linux/container_of.h>
#include <linux/sysfs.h>
#include <linux/atomic.h>
#include <linux/printk.h>
#include <linux/clk.h>
#include "register_offsets.h"

#define DEFAULT_COUNTER_STATUS		0
#define DEFAULT_ELAPSED_TIME		0
#define UT_NUM_COUNTERS			12

struct utimer_cnt {
	 /* pointer to the REAL counter device */
	struct counter_device *counter_dev;
	struct platform_device *pdev;
	void __iomem *base;
	void __iomem *chan_base[UT_NUM_COUNTERS];
	/* Spinlock to protect utimer device state */
	spinlock_t lock;
	int irq[MAX_INTERRUPTS];
	ktime_t start_time[UT_NUM_COUNTERS];
	ktime_t irq_timestamp[UT_NUM_COUNTERS];
	atomic_t ut_counter_status[UT_NUM_COUNTERS];
	atomic64_t elapsed_time_ms[UT_NUM_COUNTERS];
	/* ALARM: compare match pending */
	atomic_t alarm_fired[UT_NUM_COUNTERS];
	 /* Guard: set after events_configure */
	atomic_t events_enabled;
	/* Software pending flag for late detection */
	unsigned long cc_int_pending;
	struct work_struct work;
	struct workqueue_struct *wq;
	unsigned long pending_channels;
	u32 ced_freq;
	u32 late_detection_ticks;
	atomic_t target_overflow_count[UT_NUM_COUNTERS];
	u32 target_compare_value[UT_NUM_COUNTERS];
	enum counter_count_mode requested_mode[UT_NUM_COUNTERS];
	u32 requested_ceiling[UT_NUM_COUNTERS];
};

#define COUNTER_COMP_STATUS_TIME(_name, _read)	\
{						\
	.type = COUNTER_COMP_U64,		\
	.name = (_name),			\
	.count_u64_read = (_read),		\
}

#define COUNTER_COMP_READ_STATUS_TIME(_read)	\
	COUNTER_COMP_STATUS_TIME("counter_status", _read)

#define COUNTER_COMP_DIRECTION_RW(_read, _write)	\
{							\
	.type = COUNTER_COMP_COUNT_DIRECTION,		\
	.name = "direction_rw",				\
	.count_u32_read = (_read),			\
	.count_u8_write = (_write),			\
}

#define COUNTER_COMP_RUNNING(_read, _write)		\
	COUNTER_COMP_COUNT_BOOL("running", _read, _write)

enum utimer_count_function {
	UT_COUNT_FUNCTION_INCREASE = 0,
	UT_COUNT_FUNCTION_DECREASE,
};

static const enum counter_function utimer_count_functions[] = {
	COUNTER_FUNCTION_INCREASE,
	COUNTER_FUNCTION_DECREASE,
};

enum utimer_synapse_action {
	UT_SYN_ACT_NONE = 0,
	UT_SYN_ACT_RISING_EDGE,
	UT_SYN_ACT_FALLING_EDGE,
	UT_SYN_ACT_BOTH_EDGES,
	UT_SYN_ACT_A_RISING_B_0,
	UT_SYN_ACT_A_RISING_B_1,
	UT_SYN_ACT_A_FALLING_B_0,
	UT_SYN_ACT_A_FALLING_B_1,
	UT_SYN_ACT_B_RISING_A_0,
	UT_SYN_ACT_B_RISING_A_1,
	UT_SYN_ACT_B_FALLING_A_0,
	UT_SYN_ACT_B_FALLING_A_1,
};

static const int ut_default_irq[MAX_INTERRUPTS] = {
	[0 ... MAX_INTERRUPTS - 1] = -EINVAL,
};

enum ut_count_mode {
	UT_CNT_MODE_SAWTOOTH = 0,
	UT_CNT_MODE_TRIANGLE,
};

static int ut_counter_status_time_read(struct counter_device *counter,
				       struct counter_count *count, u64 *val)
{
	struct utimer_cnt *const ut = counter_priv(counter);
	struct device *dev = &ut->pdev->dev;
	u8 status = atomic_read(&ut->ut_counter_status[count->id]);
	u64 elapsed = atomic64_read(&ut->elapsed_time_ms[count->id]);

	dev_info(dev, "UTIMER channel %d: %s (ms)\n",
		 count->id, status ? "Overflow" : "Underflow");
	*val = elapsed;
	return 0;
}

/**
 * ALARM: Read alarm status. Returns 1 if alarm fired since last read,
 * 0 otherwise. Clears on read (edge detection).
 */
static int utimer_alarm_read(struct counter_device *counter,
			     struct counter_count *count, u64 *val)
{
	struct utimer_cnt *const ut = counter_priv(counter);

	*val = atomic_read(&ut->alarm_fired[count->id]);
	atomic_set(&ut->alarm_fired[count->id], 0);
	return 0;
}

/* ALARM: Read compare value for the A-compare alarm only */
static int utimer_compare_read(struct counter_device *counter,
			       struct counter_count *count, u64 *val)
{
	struct utimer_cnt *const ut = counter_priv(counter);

	*val = readl(ut->chan_base[count->id] + UT_COMPARE_A);
	return 0;
}

/* ALARM: Write A-compare value and enable compare match IRQ */
static int utimer_compare_write(struct counter_device *counter,
				struct counter_count *count, u64 val)
{
	struct utimer_cnt *const ut = counter_priv(counter);
	unsigned long flags;
	u32 ctrl, mask, current_val, diff;

	if (val > U32_MAX)
		return -EINVAL;

	spin_lock_irqsave(&ut->lock, flags);

	/* Write compare value */
	writel((u32)val, ut->chan_base[count->id] + UT_COMPARE_A);

	/* Reset target overflow count for standard one-shot compare writes */
	atomic_set(&ut->target_overflow_count[count->id], 0);

	/* ALARM: Enable compare match drive control */
	ctrl = readl(ut->chan_base[count->id] + UT_COMPARE_CTRL_A);
	ctrl |= COMPARE_CTRL_DRV_COMPARE_EN;
	writel(ctrl, ut->chan_base[count->id] + UT_COMPARE_CTRL_A);

	/* ALARM: Clear any pending stale interrupt */
	writel(CHAN_INTERRUPT_COMPARE_MATCH,
	       ut->chan_base[count->id] + UT_CHAN_INT);

	/* ALARM: Unmask compare match interrupt */
	mask = readl(ut->chan_base[count->id] + UT_CHAN_INT_MASK);
	mask &= ~CHAN_INTERRUPT_COMPARE_MATCH;
	writel(mask, ut->chan_base[count->id] + UT_CHAN_INT_MASK);

	/* LATE DETECTION ALGORITHM */
	current_val = readl(ut->chan_base[count->id] + UT_CNTR);
	diff = (u32)val - current_val;

	if (diff > (U32_MAX - ut->late_detection_ticks)) {
		set_bit(count->id, &ut->cc_int_pending);
		irq_set_irqchip_state(ut->irq[count->id],
				      IRQCHIP_STATE_PENDING, true);
	}

	spin_unlock_irqrestore(&ut->lock, flags);
	return 0;
}

static int counter_dir_write(struct counter_device *counter,
			     struct counter_count *count, u8 val)
{
	struct utimer_cnt *const ut = counter_priv(counter);
	unsigned long flags;
	unsigned int ctrl;

	spin_lock_irqsave(&ut->lock, flags);
	ctrl = readl(ut->chan_base[count->id] + UT_CNTR_CTRL);
	if (val)
		ctrl |= CNTR_DIR;
	else
		ctrl &= ~CNTR_DIR;
	writel(ctrl, ut->chan_base[count->id] + UT_CNTR_CTRL);
	spin_unlock_irqrestore(&ut->lock, flags);
	return 0;
}

static void utimer_irq_clear(struct utimer_cnt *ut, int channel,
			     bool is_overflow)
{
	u32 irq_clear;

	irq_clear = is_overflow ? CHAN_INTERRUPT_OVER_FLOW :
				  CHAN_INTERRUPT_UNDER_FLOW;
	writel(irq_clear, ut->chan_base[channel] + UT_CHAN_INT);
}

static void ut_clear_interrupt(struct utimer_cnt *ut, int channel)
{
	unsigned int ctrl;
	bool is_up_counter;

	ctrl = readl(ut->chan_base[channel] + UT_CNTR_CTRL);
	is_up_counter = !(ctrl & CNTR_DIR);

	if (is_up_counter)
		writel(CHAN_INTERRUPT_OVER_FLOW,
		       ut->chan_base[channel] + UT_CHAN_INT);
	else
		writel(CHAN_INTERRUPT_UNDER_FLOW,
		       ut->chan_base[channel] + UT_CHAN_INT);
}

static void ut_mask_interrupt(struct utimer_cnt *ut, int channel)
{
	unsigned int ctrl;
	u32 mask_reg;
	bool is_up_counter;

	ctrl = readl(ut->chan_base[channel] + UT_CNTR_CTRL);
	is_up_counter = !(ctrl & CNTR_DIR);

	mask_reg = readl(ut->chan_base[channel] + UT_CHAN_INT_MASK);

	if (is_up_counter)
		mask_reg |= CHAN_INTERRUPT_OVER_FLOW;
	else
		mask_reg |= CHAN_INTERRUPT_UNDER_FLOW;

	writel(mask_reg, ut->chan_base[channel] + UT_CHAN_INT_MASK);
}

static void ut_unmask_interrupt(struct utimer_cnt *ut, int channel)
{
	unsigned int ctrl;
	u32 mask_reg;
	bool is_up_counter;

	ctrl = readl(ut->chan_base[channel] + UT_CNTR_CTRL);
	is_up_counter = !(ctrl & CNTR_DIR);

	mask_reg = readl(ut->chan_base[channel] + UT_CHAN_INT_MASK);

	if (is_up_counter)
		mask_reg &= ~CHAN_INTERRUPT_OVER_FLOW;
	else
		mask_reg &= ~CHAN_INTERRUPT_UNDER_FLOW;

	writel(mask_reg, ut->chan_base[channel] + UT_CHAN_INT_MASK);
}

static inline void utimer_calculate_elapsed_time(struct utimer_cnt *ut,
						 int channel, bool is_overflow)
{
	ktime_t now = ut->irq_timestamp[channel];
	u64 elapsed_ms = ktime_to_ms(ktime_sub(now, ut->start_time[channel]));

	atomic64_set(&ut->elapsed_time_ms[channel], elapsed_ms);
	ut->start_time[channel] = now;
}

static void utimer_work_handler(struct work_struct *work)
{
	struct utimer_cnt *ut = container_of(work, struct utimer_cnt, work);
	unsigned long flags;
	bool is_overflow;
	u32 channel;

	spin_lock_irqsave(&ut->lock, flags);
	for_each_set_bit(channel, &ut->pending_channels, UT_NUM_COUNTERS) {
		is_overflow = atomic_read(&ut->ut_counter_status[channel]);
		utimer_calculate_elapsed_time(ut, channel, is_overflow);
		clear_bit(channel, &ut->pending_channels);
	}
	spin_unlock_irqrestore(&ut->lock, flags);
}

/* ALARM: Handles A-compare match interrupts only */
static irqreturn_t utimer_irq_handler(int irq, void *dev_id)
{
	struct utimer_cnt *ut = dev_id;
	unsigned long flags;
	u32 running_status, channel;
	int ovf_cnt;

	running_status = readl(ut->base + UT_GLB_CNTR_RUNNING);
	if (unlikely(!running_status))
		return IRQ_NONE;
	for (channel = 0; channel < UT_NUM_COUNTERS; channel++) {
		u32 irq_check, irq_mask;
		bool overflow = false, underflow = false;
		bool sw_pending = false;

		if (!(running_status & BIT(channel)))
			continue;
		spin_lock_irqsave(&ut->lock, flags);
		irq_check = readl(ut->chan_base[channel] + UT_CHAN_INT);
		irq_mask = readl(ut->chan_base[channel] + UT_CHAN_INT_MASK);
		overflow = (irq_check & CHAN_INTERRUPT_OVER_FLOW) &&
			   !(irq_mask & CHAN_INTERRUPT_OVER_FLOW);
		underflow = (irq_check & CHAN_INTERRUPT_UNDER_FLOW) &&
			    !(irq_mask & CHAN_INTERRUPT_UNDER_FLOW);
		sw_pending = test_and_clear_bit(channel, &ut->cc_int_pending);
		if (sw_pending ||
		    ((irq_check & CHAN_INTERRUPT_COMPARE_MATCH) &&
		     !(irq_mask & CHAN_INTERRUPT_COMPARE_MATCH))) {
			if (!sw_pending)
				writel(CHAN_INTERRUPT_COMPARE_MATCH,
				       ut->chan_base[channel] + UT_CHAN_INT);
			atomic_t *t_ovf = &ut->target_overflow_count[channel];

			ovf_cnt = atomic_read(t_ovf);

			if (ovf_cnt > 0) {
				/* Simulated overflow via COMPARE_MATCH! */
				ovf_cnt--;
				if (ovf_cnt == 0) {
					u32 current_val, diff, tcv;
					void __iomem *cb;

					tcv = ut->target_compare_value[channel];
					cb = ut->chan_base[channel];

					writel(tcv, cb + UT_COMPARE_A);
					/* Late detection check */
					current_val = readl(cb + UT_CNTR);
					diff = tcv - current_val;
					if (diff > (U32_MAX -
					    ut->late_detection_ticks)) {
						int irq = ut->irq[channel];
						enum irqchip_irq_state st =
							IRQCHIP_STATE_PENDING;

						set_bit(channel,
							&ut->cc_int_pending);
						irq_set_irqchip_state(irq, st,
								      true);
					}
				} else {
					/* Wait for another overflow chunk */
					writel(U32_MAX,
					       ut->chan_base[channel] +
					       UT_COMPARE_A);
				}
				/* Re-enable COMPARE_EN */
				writel(readl(ut->chan_base[channel] +
					     UT_COMPARE_CTRL_A) |
				       COMPARE_CTRL_DRV_COMPARE_EN,
				       ut->chan_base[channel] +
				       UT_COMPARE_CTRL_A);
				atomic_set(&ut->target_overflow_count[channel],
					   ovf_cnt);
			} else {
				/* Disable compare match hardware */
				writel(readl(ut->chan_base[channel] +
					     UT_COMPARE_CTRL_A) &
				       ~COMPARE_CTRL_DRV_COMPARE_EN,
				       ut->chan_base[channel] +
				       UT_COMPARE_CTRL_A);
				writel(readl(ut->chan_base[channel] +
					     UT_CHAN_INT_MASK) |
				       CHAN_INTERRUPT_COMPARE_MATCH,
				       ut->chan_base[channel] +
				       UT_CHAN_INT_MASK);
				atomic_set(&ut->alarm_fired[channel], 1);

				if (atomic_read(&ut->events_enabled)) {
					u32 ev = COUNTER_EVENT_THRESHOLD;

					counter_push_event(ut->counter_dev, ev,
							   channel);
				}
			}
		}
		if (overflow || underflow) {
			utimer_irq_clear(ut, channel, overflow);
			ut->irq_timestamp[channel] = ktime_get();
			atomic_set(&ut->ut_counter_status[channel], overflow);
			set_bit(channel, &ut->pending_channels);
			if (overflow) {
				if (ut->requested_mode[channel] ==
				    COUNTER_COUNT_MODE_RANGE_LIMIT) {
					void __iomem *cb;
					u32 ceiling;

					cb = ut->chan_base[channel];
					ceiling = readl(cb + UT_CNTR_PTR);
					/* Stop the counter and clear run bit */
					writel(BIT(channel),
					       ut->base + UT_GLB_CNTR_STOP);
					writel(BIT(channel),
					       ut->base + UT_GLB_CNTR_CLEAR);
					/* Mask interrupt so it doesn't fire */
					ut_mask_interrupt(ut, channel);
					writel(ceiling, ut->chan_base[channel] +
					       UT_CNTR);
				} else if (ut->requested_mode[channel] ==
					   COUNTER_COUNT_MODE_NON_RECYCLE) {
					writel(BIT(channel),
					       ut->base + UT_GLB_CNTR_STOP);
					writel(BIT(channel),
					       ut->base + UT_GLB_CNTR_CLEAR);
					ut_mask_interrupt(ut, channel);
					writel(U32_MAX, ut->chan_base[channel] +
					       UT_CNTR);
				}
			}
			if (atomic_read(&ut->events_enabled)) {
				u32 ev = overflow ? COUNTER_EVENT_OVERFLOW :
					 COUNTER_EVENT_UNDERFLOW;

				counter_push_event(ut->counter_dev, ev,
						   channel);
			}
		}
		spin_unlock_irqrestore(&ut->lock, flags);
	}
	if (ut->pending_channels)
		queue_work(ut->wq, &ut->work);
	return IRQ_HANDLED;
}

static int ut_get_dir(struct counter_device *counter,
		      struct counter_count *count,
		      enum counter_count_direction *dir)
{
	struct utimer_cnt *const ut = counter_priv(counter);
	unsigned int ud_flag;

	ud_flag = readl(ut->chan_base[count->id] + UT_CNTR_CTRL) & CNTR_DIR;
	*dir = (ud_flag) ? COUNTER_COUNT_DIRECTION_BACKWARD :
			   COUNTER_COUNT_DIRECTION_FORWARD;
	return 0;
}

static int ut_get_ptr(struct counter_device *counter,
		      struct counter_count *count, u64 *ptr)
{
	struct utimer_cnt *const ut = counter_priv(counter);

	if (ut->requested_mode[count->id] == COUNTER_COUNT_MODE_NON_RECYCLE ||
	    ut->requested_mode[count->id] == COUNTER_COUNT_MODE_NORMAL)
		*ptr = ut->requested_ceiling[count->id];
	else
		*ptr = readl(ut->chan_base[count->id] + UT_CNTR_PTR);
	return 0;
}

static int ut_count_read(struct counter_device *counter,
			 struct counter_count *count, u64 *val)
{
	struct utimer_cnt *const ut = counter_priv(counter);

	*val = readl(ut->chan_base[count->id] + UT_CNTR);
	return 0;
}

static int ut_count_write(struct counter_device *counter,
			  struct counter_count *count, u64 val)
{
	struct utimer_cnt *const ut = counter_priv(counter);
	unsigned long flags;
	unsigned int ctrl;

	if (val > U32_MAX) {
		dev_err(&ut->pdev->dev, "Value too large for 32-bit register\n");
		return -EINVAL;
	}

	spin_lock_irqsave(&ut->lock, flags);
	ctrl = readl(ut->chan_base[count->id] + UT_CNTR_CTRL);
	ctrl &= ~CNTR_START;
	writel(ctrl, ut->chan_base[count->id] + UT_CNTR_CTRL);
	writel((unsigned int)val, ut->chan_base[count->id] + UT_CNTR);
	spin_unlock_irqrestore(&ut->lock, flags);
	return 0;
}

static int ut_function_get(struct counter_device *counter,
			   struct counter_count *count,
			   enum counter_function *function)
{
	enum counter_count_direction dir;
	u32 err;

	err = ut_get_dir(counter, count, &dir);
	if (err)
		return err;

	if (dir == COUNTER_COUNT_DIRECTION_FORWARD)
		*function = COUNTER_FUNCTION_INCREASE;
	else
		*function = COUNTER_FUNCTION_DECREASE;

	return 0;
}

static int ut_action_get(struct counter_device *counter,
			 struct counter_count *count,
			 struct counter_synapse *synapse,
			 enum counter_synapse_action *action)
{
	enum counter_function function;
	u32 err;

	err = ut_function_get(counter, count, &function);
	if (err)
		return err;

	*action = COUNTER_SYNAPSE_ACTION_NONE;
	switch (function) {
	case COUNTER_FUNCTION_INCREASE:
		*action = COUNTER_SYNAPSE_ACTION_FALLING_EDGE;
		break;
	case COUNTER_FUNCTION_DECREASE:
		*action = COUNTER_SYNAPSE_ACTION_BOTH_EDGES;
		break;
	default:
		*action = COUNTER_SYNAPSE_ACTION_NONE;
		return -EOPNOTSUPP;
	}
	return 0;
}

static int utimer_count_mode_get(struct counter_device *counter,
				 struct counter_count *count,
				 enum counter_count_mode *cnt_mode)
{
	struct utimer_cnt *const ut = counter_priv(counter);

	*cnt_mode = ut->requested_mode[count->id];
	return 0;
}

static int utimer_count_mode_set(struct counter_device *counter,
				 struct counter_count *count,
				 enum counter_count_mode cnt_mode)
{
	struct utimer_cnt *const ut = counter_priv(counter);
	unsigned int val, hw_mode;
	unsigned long flags;

	if (cnt_mode == COUNTER_COUNT_MODE_RANGE_LIMIT ||
	    cnt_mode == COUNTER_COUNT_MODE_NON_RECYCLE)
		hw_mode = 1;
	else
		hw_mode = 0;
	spin_lock_irqsave(&ut->lock, flags);
	if (cnt_mode == COUNTER_COUNT_MODE_NON_RECYCLE ||
	    cnt_mode == COUNTER_COUNT_MODE_NORMAL) {
		if (ut->requested_mode[count->id] !=
		    COUNTER_COUNT_MODE_NON_RECYCLE &&
		    ut->requested_mode[count->id] !=
		    COUNTER_COUNT_MODE_NORMAL) {
			ut->requested_ceiling[count->id] =
				readl(ut->chan_base[count->id] + UT_CNTR_PTR);
			writel(U32_MAX, ut->chan_base[count->id] + UT_CNTR_PTR);
		} else {
			writel(U32_MAX, ut->chan_base[count->id] + UT_CNTR_PTR);
		}
	} else {
		if (ut->requested_mode[count->id] ==
		    COUNTER_COUNT_MODE_NON_RECYCLE ||
		    ut->requested_mode[count->id] ==
		    COUNTER_COUNT_MODE_NORMAL) {
			writel(ut->requested_ceiling[count->id],
			       ut->chan_base[count->id] + UT_CNTR_PTR);
		}
	}
	val = readl(ut->chan_base[count->id] + UT_CNTR_CTRL);
	val &= ~(CNTR_TYPE_MASK << CNTR_TYPE_SHIFT);
	val |= hw_mode << CNTR_TYPE_SHIFT;
	writel(val, ut->chan_base[count->id] + UT_CNTR_CTRL);
	ut->requested_mode[count->id] = cnt_mode;
	spin_unlock_irqrestore(&ut->lock, flags);
	return 0;
}

static int utimer_count_enable_read(struct counter_device *counter,
				    struct counter_count *count, u8 *enable)
{
	struct utimer_cnt *const ut = counter_priv(counter);

	*enable = !!(readl(ut->chan_base[count->id] + UT_CNTR_CTRL) & CNTR_EN);
	return 0;
}

static int utimer_count_enable_write(struct counter_device *counter,
				     struct counter_count *count, u8 enable)
{
	struct utimer_cnt *const ut = counter_priv(counter);
	unsigned long flags;
	unsigned int val;

	spin_lock_irqsave(&ut->lock, flags);
	val = readl(ut->chan_base[count->id] + UT_CNTR_CTRL);
	if (enable)
		val |= CNTR_EN;
	else
		val &= ~CNTR_EN;
	writel(val, ut->chan_base[count->id] + UT_CNTR_CTRL);
	spin_unlock_irqrestore(&ut->lock, flags);
	return 0;
}

static int utimer_count_ptr_read(struct counter_device *counter,
				 struct counter_count *count, u64 *ptr)
{
	return ut_get_ptr(counter, count, ptr);
}

static int utimer_count_ptr_write(struct counter_device *counter,
				  struct counter_count *count, u64 ptr)
{
	struct utimer_cnt *const ut = counter_priv(counter);
	unsigned long flags;

	if (ptr > U32_MAX) {
		dev_err(&ut->pdev->dev, "Value too large for 32-bit register\n");
		return -EINVAL;
	}
	spin_lock_irqsave(&ut->lock, flags);
	if (ut->requested_mode[count->id] == COUNTER_COUNT_MODE_NON_RECYCLE ||
	    ut->requested_mode[count->id] == COUNTER_COUNT_MODE_NORMAL) {
		ut->requested_ceiling[count->id] = (unsigned int)ptr;
		writel(U32_MAX, ut->chan_base[count->id] + UT_CNTR_PTR);
	} else {
		writel((unsigned int)ptr,
		       ut->chan_base[count->id] + UT_CNTR_PTR);
	}
	spin_unlock_irqrestore(&ut->lock, flags);
	return 0;
}

static int utimer_count_running_read(struct counter_device *counter,
				     struct counter_count *count, u8 *val)
{
	struct utimer_cnt *const ut = counter_priv(counter);

	*val = !!(readl(ut->base + UT_GLB_CNTR_RUNNING) & BIT(count->id));
	return 0;
}

static int utimer_count_running_write(struct counter_device *counter,
				      struct counter_count *count, u8 val)
{
	struct utimer_cnt *const ut = counter_priv(counter);
	unsigned long flags;

	spin_lock_irqsave(&ut->lock, flags);
	ut->start_time[count->id] = ktime_get();
	if (val) {
		ut_unmask_interrupt(ut, count->id);
		ut_clear_interrupt(ut, count->id);
		writel(BIT(count->id), ut->base + UT_GLB_CNTR_START);
	} else {
		writel(BIT(count->id), ut->base + UT_GLB_CNTR_STOP);
		writel(BIT(count->id), ut->base + UT_GLB_CNTR_CLEAR);
		ut_mask_interrupt(ut, count->id);
		ut_clear_interrupt(ut, count->id);
	}
	spin_unlock_irqrestore(&ut->lock, flags);
	return 0;
}

/**
 * ==========================================================
 * GENERIC ALARM INTERFACE
 * ==========================================================
 */
static int utimer_delay_alarm_read(struct counter_device *counter,
				   struct counter_count *count,
				   u64 *val)
{
	*val = 0;
	return 0;
}

/* ALARM: Programs A-compare register for delayed alarm */
static int utimer_delay_alarm_write(struct counter_device *counter,
				    struct counter_count *count, u64 val)
{
	struct utimer_cnt *const ut = counter_priv(counter);
	u64 total_ticks;
	u32 target_compare, required_overflows, current_cnt;
	unsigned long flags;

	if (val == 0)
		return -EINVAL;
	/**
	 * val is expected in nanoseconds,So it will Convert
	 * nanoseconds to hardware ticks:
	 * then ticks will set for alarm
	 * ticks = (nanoseconds * ced_freq) / NSEC_PER_SEC
	 */
	total_ticks = mul_u64_u64_div_u64(val, ut->ced_freq, NSEC_PER_SEC);
	required_overflows = (u32)(total_ticks >> 32);

	spin_lock_irqsave(&ut->lock, flags);
	current_cnt = readl(ut->chan_base[count->id] + UT_CNTR);
	target_compare = current_cnt + (u32)total_ticks;
	if (target_compare < current_cnt)
		required_overflows++;
	ut->target_compare_value[count->id] = target_compare;
	if (required_overflows == 0) {
		u32 current_val, diff;

		writel(target_compare, ut->chan_base[count->id] + UT_COMPARE_A);
		writel(readl(ut->chan_base[count->id] + UT_COMPARE_CTRL_A) |
		       COMPARE_CTRL_DRV_COMPARE_EN,
		       ut->chan_base[count->id] + UT_COMPARE_CTRL_A);
		writel(CHAN_INTERRUPT_COMPARE_MATCH,
		       ut->chan_base[count->id] + UT_CHAN_INT);
		writel(readl(ut->chan_base[count->id] + UT_CHAN_INT_MASK) &
		       ~CHAN_INTERRUPT_COMPARE_MATCH,
		       ut->chan_base[count->id] + UT_CHAN_INT_MASK);
		atomic_set(&ut->target_overflow_count[count->id], 0);

		/* Late detection algorithm */
		current_val = readl(ut->chan_base[count->id] + UT_CNTR);
		diff = target_compare - current_val;
		if (diff > (U32_MAX - ut->late_detection_ticks)) {
			set_bit(count->id, &ut->cc_int_pending);
			irq_set_irqchip_state(ut->irq[count->id],
					      IRQCHIP_STATE_PENDING, true);
		}
	} else {
		u32 cnt_ctrl;

		atomic_set(&ut->target_overflow_count[count->id],
			   required_overflows);
		/* Force Sawtooth mode & UP counting */
		cnt_ctrl = readl(ut->chan_base[count->id] + UT_CNTR_CTRL);
		cnt_ctrl &= ~(CNTR_TYPE_MASK << CNTR_TYPE_SHIFT);
		cnt_ctrl &= ~CNTR_DIR;
		writel(cnt_ctrl, ut->chan_base[count->id] + UT_CNTR_CTRL);
		writel(U32_MAX, ut->chan_base[count->id] + UT_CNTR_PTR);
		writel(U32_MAX, ut->chan_base[count->id] + UT_COMPARE_A);
		writel(readl(ut->chan_base[count->id] + UT_COMPARE_CTRL_A) |
		       COMPARE_CTRL_DRV_COMPARE_EN,
		       ut->chan_base[count->id] + UT_COMPARE_CTRL_A);
		writel(CHAN_INTERRUPT_COMPARE_MATCH,
		       ut->chan_base[count->id] + UT_CHAN_INT);
		writel(readl(ut->chan_base[count->id] + UT_CHAN_INT_MASK) &
		       ~CHAN_INTERRUPT_COMPARE_MATCH,
		       ut->chan_base[count->id] + UT_CHAN_INT_MASK);
	}
	spin_unlock_irqrestore(&ut->lock, flags);

	return 0;
}

static const enum counter_count_mode utimer_cnt_modes[] = {
	COUNTER_COUNT_MODE_NORMAL,
	COUNTER_COUNT_MODE_RANGE_LIMIT,
	COUNTER_COUNT_MODE_NON_RECYCLE,
	COUNTER_COUNT_MODE_MODULO_N,
};

static DEFINE_COUNTER_AVAILABLE(utimer_count_mode_available, utimer_cnt_modes);
/* ALARM: Added "alarm" attribute to extension array */
static struct counter_comp utimer_count_ext[] = {
	COUNTER_COMP_CEILING(utimer_count_ptr_read, utimer_count_ptr_write),
	COUNTER_COMP_COUNT_MODE(utimer_count_mode_get,
				utimer_count_mode_set,
				utimer_count_mode_available),
	COUNTER_COMP_DIRECTION_RW(ut_get_dir, counter_dir_write),
	COUNTER_COMP_ENABLE(utimer_count_enable_read,
			    utimer_count_enable_write),
	COUNTER_COMP_RUNNING(utimer_count_running_read,
			     utimer_count_running_write),
	COUNTER_COMP_READ_STATUS_TIME(ut_counter_status_time_read),
	COUNTER_COMP_COUNT_U64("compare_value", utimer_compare_read,
			       utimer_compare_write),
	COUNTER_COMP_COUNT_U64("alarm", utimer_alarm_read, NULL),
	COUNTER_COMP_COUNT_U64("delay_alarm", utimer_delay_alarm_read,
			       utimer_delay_alarm_write),
};

#define UT_EVENTS_IN_FOR_CHANNEL_A(ch)	\
	{ .id = (12 + ((ch) * 2)), .name = "Channel " #ch " event A" }
#define UT_EVENTS_IN_FOR_CHANNEL_B(ch)	\
	{ .id = (12 + ((ch) * 2) + 1), .name = "Channel " #ch " event B" }

static struct counter_signal utimer_signals[] = {
	{ .id = 0, .name = "trigger 0" },
	{ .id = 1, .name = "trigger 1" },
	{ .id = 2, .name = "trigger 2" },
	{ .id = 3, .name = "trigger 3" },
	{ .id = 4, .name = "glb events out 3" },
	{ .id = 5, .name = "glb events out 2" },
	{ .id = 6, .name = "glb events out 1" },
	{ .id = 7, .name = "glb events out 0" },
	{ .id = 8, .name = "comp4_filter_out" },
	{ .id = 9, .name = "comp3_filter_out" },
	{ .id = 10, .name = "comp2_filter_out" },
	{ .id = 11, .name = "comp1_filter_out" },
	UT_EVENTS_IN_FOR_CHANNEL_A(0), UT_EVENTS_IN_FOR_CHANNEL_B(0),
	UT_EVENTS_IN_FOR_CHANNEL_A(1), UT_EVENTS_IN_FOR_CHANNEL_B(1),
	UT_EVENTS_IN_FOR_CHANNEL_A(2), UT_EVENTS_IN_FOR_CHANNEL_B(2),
	UT_EVENTS_IN_FOR_CHANNEL_A(3), UT_EVENTS_IN_FOR_CHANNEL_B(3),
	UT_EVENTS_IN_FOR_CHANNEL_A(4), UT_EVENTS_IN_FOR_CHANNEL_B(4),
	UT_EVENTS_IN_FOR_CHANNEL_A(5), UT_EVENTS_IN_FOR_CHANNEL_B(5),
	UT_EVENTS_IN_FOR_CHANNEL_A(6), UT_EVENTS_IN_FOR_CHANNEL_B(6),
	UT_EVENTS_IN_FOR_CHANNEL_A(7), UT_EVENTS_IN_FOR_CHANNEL_B(7),
	UT_EVENTS_IN_FOR_CHANNEL_A(8), UT_EVENTS_IN_FOR_CHANNEL_B(8),
	UT_EVENTS_IN_FOR_CHANNEL_A(9), UT_EVENTS_IN_FOR_CHANNEL_B(9),
	UT_EVENTS_IN_FOR_CHANNEL_A(10), UT_EVENTS_IN_FOR_CHANNEL_B(10),
	UT_EVENTS_IN_FOR_CHANNEL_A(11), UT_EVENTS_IN_FOR_CHANNEL_B(11),
	UT_EVENTS_IN_FOR_CHANNEL_A(12), UT_EVENTS_IN_FOR_CHANNEL_B(12),
	UT_EVENTS_IN_FOR_CHANNEL_A(13), UT_EVENTS_IN_FOR_CHANNEL_B(13),
	UT_EVENTS_IN_FOR_CHANNEL_A(14), UT_EVENTS_IN_FOR_CHANNEL_B(14),
	UT_EVENTS_IN_FOR_CHANNEL_A(15), UT_EVENTS_IN_FOR_CHANNEL_B(15),
};

static const enum counter_synapse_action ut_synapse_trigger_actions[] = {
	[UT_SYN_ACT_NONE] = COUNTER_SYNAPSE_ACTION_NONE,
	[UT_SYN_ACT_RISING_EDGE] = COUNTER_SYNAPSE_ACTION_RISING_EDGE,
	[UT_SYN_ACT_FALLING_EDGE] = COUNTER_SYNAPSE_ACTION_FALLING_EDGE,
	[UT_SYN_ACT_BOTH_EDGES] = COUNTER_SYNAPSE_ACTION_BOTH_EDGES
};

static const enum counter_synapse_action ut_synapse_chan_event_actions[] = {
	[UT_SYN_ACT_NONE] = COUNTER_SYNAPSE_ACTION_NONE,
	[UT_SYN_ACT_A_RISING_B_0] = COUNTER_SYNAPSE_ACTION_BOTH_EDGES,
	[UT_SYN_ACT_A_RISING_B_1] = COUNTER_SYNAPSE_ACTION_BOTH_EDGES,
	[UT_SYN_ACT_A_FALLING_B_0] = COUNTER_SYNAPSE_ACTION_BOTH_EDGES,
	[UT_SYN_ACT_A_FALLING_B_1] = COUNTER_SYNAPSE_ACTION_BOTH_EDGES,
	[UT_SYN_ACT_B_RISING_A_0] = COUNTER_SYNAPSE_ACTION_BOTH_EDGES,
	[UT_SYN_ACT_B_RISING_A_1] = COUNTER_SYNAPSE_ACTION_BOTH_EDGES,
	[UT_SYN_ACT_B_FALLING_A_0] = COUNTER_SYNAPSE_ACTION_BOTH_EDGES,
	[UT_SYN_ACT_B_FALLING_A_1] = COUNTER_SYNAPSE_ACTION_BOTH_EDGES,
};

static const enum counter_synapse_action ut_synapse_glb_event_actions[] = {
	[UT_SYN_ACT_NONE] = COUNTER_SYNAPSE_ACTION_NONE,
	[UT_SYN_ACT_RISING_EDGE] = COUNTER_SYNAPSE_ACTION_RISING_EDGE,
};

#define UT_COUNT_SYNAPSE_TRIGGER(id) {				\
	.actions_list = ut_synapse_trigger_actions,		\
	.num_actions = ARRAY_SIZE(ut_synapse_trigger_actions),	\
	.signal = utimer_signals + (id)				\
}

#define UT_COUNT_SYNAPSE_GLB_EVENT(id) {			\
	.actions_list = ut_synapse_glb_event_actions,		\
	.num_actions = ARRAY_SIZE(ut_synapse_glb_event_actions),\
	.signal = utimer_signals + (id)				\
}

#define UT_COUNT_SYNAPSE_CHAN_EVENT(ch)	{			\
	.actions_list = ut_synapse_chan_event_actions,		\
	.num_actions = ARRAY_SIZE(ut_synapse_chan_event_actions),\
	.signal = utimer_signals + 12 + (2 * (ch))		\
}

#define UT_COUNT_SYNAPSES_CHANNEL(ch)	{			\
	UT_COUNT_SYNAPSE_TRIGGER(0), UT_COUNT_SYNAPSE_TRIGGER(1), \
	UT_COUNT_SYNAPSE_TRIGGER(2), UT_COUNT_SYNAPSE_TRIGGER(3), \
	UT_COUNT_SYNAPSE_GLB_EVENT(4), UT_COUNT_SYNAPSE_GLB_EVENT(5), \
	UT_COUNT_SYNAPSE_GLB_EVENT(6), UT_COUNT_SYNAPSE_GLB_EVENT(7), \
	UT_COUNT_SYNAPSE_GLB_EVENT(8), UT_COUNT_SYNAPSE_GLB_EVENT(9), \
	UT_COUNT_SYNAPSE_GLB_EVENT(10), UT_COUNT_SYNAPSE_GLB_EVENT(11), \
	UT_COUNT_SYNAPSE_CHAN_EVENT(ch), \
}

static struct counter_synapse utimer_count_synapses[][13] = {
	UT_COUNT_SYNAPSES_CHANNEL(0),
	UT_COUNT_SYNAPSES_CHANNEL(1),
	UT_COUNT_SYNAPSES_CHANNEL(2),
	UT_COUNT_SYNAPSES_CHANNEL(3),
	UT_COUNT_SYNAPSES_CHANNEL(4),
	UT_COUNT_SYNAPSES_CHANNEL(5),
	UT_COUNT_SYNAPSES_CHANNEL(6),
	UT_COUNT_SYNAPSES_CHANNEL(7),
	UT_COUNT_SYNAPSES_CHANNEL(8),
	UT_COUNT_SYNAPSES_CHANNEL(9),
	UT_COUNT_SYNAPSES_CHANNEL(10),
	UT_COUNT_SYNAPSES_CHANNEL(11),
	UT_COUNT_SYNAPSES_CHANNEL(12),
	UT_COUNT_SYNAPSES_CHANNEL(13),
	UT_COUNT_SYNAPSES_CHANNEL(14),
	UT_COUNT_SYNAPSES_CHANNEL(15),
};

#define UTIMER_COUNT(_chan, _cntname, _syn) {			\
	.id = (_chan),						\
	.name = (_cntname),					\
	.functions_list = utimer_count_functions,		\
	.num_functions = ARRAY_SIZE(utimer_count_functions),	\
	.synapses = (_syn),					\
	.num_synapses = 13,					\
	.ext = utimer_count_ext,				\
	.num_ext = ARRAY_SIZE(utimer_count_ext)			\
}

static struct counter_count utimer_counts[] = {
	UTIMER_COUNT(0,  "Channel 0",  utimer_count_synapses[0]),
	UTIMER_COUNT(1,  "Channel 1",  utimer_count_synapses[1]),
	UTIMER_COUNT(2,  "Channel 2",  utimer_count_synapses[2]),
	UTIMER_COUNT(3,  "Channel 3",  utimer_count_synapses[3]),
	UTIMER_COUNT(4,  "Channel 4",  utimer_count_synapses[4]),
	UTIMER_COUNT(5,  "Channel 5",  utimer_count_synapses[5]),
	UTIMER_COUNT(6,  "Channel 6",  utimer_count_synapses[6]),
	UTIMER_COUNT(7,  "Channel 7",  utimer_count_synapses[7]),
	UTIMER_COUNT(8,  "Channel 8",  utimer_count_synapses[8]),
	UTIMER_COUNT(9,  "Channel 9",  utimer_count_synapses[9]),
	UTIMER_COUNT(10, "Channel 10", utimer_count_synapses[10]),
	UTIMER_COUNT(11, "Channel 11", utimer_count_synapses[11]),
};

static void utimer_channel_init(struct utimer_cnt *ut, int ch)
{
	u32 ctrl;

	writel(CHAN_INTERRUPT_OVER_FLOW | CHAN_INTERRUPT_UNDER_FLOW,
	       ut->chan_base[ch] + UT_CHAN_INT);
	writel(CNTR_SRC1_PGM_EN, ut->chan_base[ch] + UT_START_1_SRC);
	writel(CNTR_SRC1_PGM_EN, ut->chan_base[ch] + UT_STOP_1_SRC);
	writel(CNTR_SRC1_PGM_EN, ut->chan_base[ch] + UT_CLEAR_1_SRC);
	writel((u32)~(CHAN_INTERRUPT_OVER_FLOW | CHAN_INTERRUPT_UNDER_FLOW),
	       ut->chan_base[ch] + UT_CHAN_INT_MASK);
	ctrl = readl(ut->chan_base[ch] + UT_CNTR_CTRL);
	ctrl &= ~CNTR_DIR;
	writel(ctrl, ut->chan_base[ch] + UT_CNTR_CTRL);
}

/* ALARM: Enables threshold events via A-compare match only */
static int utimer_events_configure(struct counter_device *counter)
{
	struct utimer_cnt *const ut = counter_priv(counter);
	struct counter_event_node *event_node;
	unsigned long flags;

	spin_lock_irqsave(&ut->lock, flags);

	list_for_each_entry(event_node, &counter->events_list, l) {
		if (event_node->event == COUNTER_EVENT_THRESHOLD) {
			u32 chan_id = event_node->channel;
			u32 ctrl, mask;

			if (chan_id >= UT_NUM_COUNTERS) {
				spin_unlock_irqrestore(&ut->lock, flags);
				return -EINVAL;
			}

			ctrl = readl(ut->chan_base[chan_id] +
				     UT_COMPARE_CTRL_A);
			ctrl |= COMPARE_CTRL_DRV_COMPARE_EN;
			writel(ctrl, ut->chan_base[chan_id] +
			       UT_COMPARE_CTRL_A);
			writel(CHAN_INTERRUPT_COMPARE_MATCH,
			       ut->chan_base[chan_id] + UT_CHAN_INT);
			mask = readl(ut->chan_base[chan_id] + UT_CHAN_INT_MASK);
			mask &= ~CHAN_INTERRUPT_COMPARE_MATCH;
			writel(mask, ut->chan_base[chan_id] + UT_CHAN_INT_MASK);
		}
	}
	spin_unlock_irqrestore(&ut->lock, flags);
	/* Mark events as enabled so IRQ handler knows it's safe to push */
	atomic_set(&ut->events_enabled, 1);
	return 0;
}

static const struct counter_ops utimer_ops = {
	.count_read = ut_count_read,
	.count_write = ut_count_write,
	.function_read = ut_function_get,
	.action_read = ut_action_get,
	.events_configure = utimer_events_configure,
};

static int utimer_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct utimer_cnt *ut;
	struct counter_device *counter;
	struct clk *clk;
	unsigned int i;
	unsigned int ret;

	counter = devm_counter_alloc(dev, sizeof(*ut));
	if (!counter) {
		dev_err(dev, "Failed to allocate counter device\n");
		return -ENOMEM;
	}
	ut = counter_priv(counter);
	 /* Store real counter for IRQ handler */
	ut->counter_dev = counter;
	platform_set_drvdata(pdev, ut);
	ut->pdev = pdev;
	spin_lock_init(&ut->lock);
	counter->name = dev_name(dev);
	counter->parent = dev;
	counter->ops = &utimer_ops;
	counter->counts = utimer_counts;
	counter->num_counts = ARRAY_SIZE(utimer_counts);
	counter->signals = utimer_signals;
	counter->num_signals = ARRAY_SIZE(utimer_signals);
	memcpy(ut->irq, ut_default_irq, sizeof(ut->irq));
	ut->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(ut->base)) {
		ret = PTR_ERR(ut->base);
		dev_err_probe(dev, ret,
			      "Failed to map I/O resource at index 0\n");
		return ret;
	}
	writel(UTIMER_CLK_ENABLE, ut->base + UT_GLB_DRIVER_CLK_ENABLE);
	ut->wq = alloc_workqueue("utimer_wq", WQ_UNBOUND | WQ_HIGHPRI, 1);
	if (!ut->wq) {
		dev_err(dev, "Failed to create workqueue\n");
		return -ENOMEM;
	}
	INIT_WORK(&ut->work, utimer_work_handler);
	ut->pending_channels = 0;
	ut->cc_int_pending = 0;
	for (i = 0; i < UT_NUM_COUNTERS; i++) {
		ut->start_time[i] = ktime_set(0, 0);
		ut->irq_timestamp[i] = ktime_set(0, 0);
		/* ALARM: Initialize alarm_fired */
		atomic_set(&ut->alarm_fired[i], 0);
		atomic_set(&ut->events_enabled, 0);
		atomic_set(&ut->target_overflow_count[i], 0);
		ut->target_compare_value[i] = 0;
		ut->chan_base[i] = ut->base + UTIMER_OFFSET * (i + 1);
		utimer_channel_init(ut, i);
		ut->requested_mode[i] = COUNTER_COUNT_MODE_NORMAL;
		ut->requested_ceiling[i] = U32_MAX;
		atomic_set(&ut->ut_counter_status[i], DEFAULT_COUNTER_STATUS);
		atomic64_set(&ut->elapsed_time_ms[i], DEFAULT_ELAPSED_TIME);
	}
	for (i = 0; i < MAX_INTERRUPTS; i++) {
		int irq_num = platform_get_irq(pdev, i);

		if (irq_num < 0) {
			dev_err(dev,
				"IRQ %d missing, search stopped at %d interrupts\n",
				i, i);
			break;
		}
		ut->irq[i] = irq_num;
		ret = devm_request_irq(dev, ut->irq[i], utimer_irq_handler,
				       IRQF_SHARED, dev_name(dev), ut);
		if (ret) {
			dev_err(dev, "Failed to request IRQ %d: %d\n", i, ret);
			goto err_free_wq;
		}
	}
	writel(DRIVER_OUT_ENABLE, ut->base + UT_GLB_DRIVER_OEN);
	/* UTIMER base clock frequency */
	clk = devm_clk_get(dev, NULL);
	if (IS_ERR(clk)) {
		ret = PTR_ERR(clk);
		dev_err_probe(dev, ret, "Failed to get clock\n");
		goto err_free_wq;
	}
	ut->ced_freq = clk_get_rate(clk);
		ut->late_detection_ticks =
			(u32)mul_u64_u64_div_u64(UTIMER_LATE_DETECTION_NSEC,
					ut->ced_freq, NSEC_PER_SEC);
	ret = devm_counter_add(dev, counter);
	if (ret) {
		dev_err_probe(dev, ret, "Failed to register counter device\n");
		goto err_free_wq;
	}
	return 0;
err_free_wq:
	destroy_workqueue(ut->wq);
	return ret;
}

static const struct of_device_id utimer_of_match[] = {
	{ .compatible = "alif,alif-utimer", },
	{},
};
MODULE_DEVICE_TABLE(of, utimer_of_match);

static struct platform_driver utimer_driver = {
	.probe = utimer_probe,
	.driver = {
		.name = "utimer",
		.of_match_table = utimer_of_match,
	},
};
module_platform_driver(utimer_driver);
MODULE_AUTHOR("Harith George <harith.g@alifsemi.com>");
MODULE_DESCRIPTION("Alif utimer driver");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS(COUNTER);
