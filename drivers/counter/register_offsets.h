/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Register definitions for Alif Semiconductor UTIMER IP
 *
 * Copyright (C) 2021-2025 Alif Semiconductor
 */

#ifndef __ALIF_UTIMER_REGISTERS_H
#define __ALIF_UTIMER_REGISTERS_H

#include <linux/bits.h>

/* Global Configuration */
#define UTIMER_OFFSET                   0x1000          /* Offset between channel register blocks */
#define UT_NUM_COUNTERS                 12              /* Number of counter channels */
#define MAX_INTERRUPTS                  96              /* Maximum number of interrupts */

/* Default Values */
#define DRIVER_OUT_ENABLE               0xffffffff      /* Driver output enable mask */
#define UTIMER_CLK_ENABLE               0xffff          /* Clock enable mask */
#define UTIMER_CLK_DISABLE              0x0             /* Clock disable value */
#define DEFAULT_COUNTER_STATUS          0               /* Initial counter status */
#define DEFAULT_ELAPSED_TIME            0               /* Initial elapsed time */

/* Channel Registers */
#define UT_CNTR_CTRL                    0x80            /* Channel Control Register offset */

/* UT_CNTR_CTRL bits */
#define CNTR_EN                         BIT(0)          /* Counter enable bit */
#define CNTR_START                      BIT(1)          /* Counter start trigger */
#define CNTR_TYPE_SHIFT                 2               /* Counter mode field shift */
#define CNTR_DIR                        BIT(8)          /* Count direction (0 = up, 1 = down) */
#define CNTR_TYPE_MASK                  0x7             /* Counter mode bitmask */
#define UT_START_1_SRC                  0x4             /* Start source config register */
#define UT_STOP_1_SRC                   0xc             /* Stop source config register */
#define UT_CLEAR_1_SRC                  0x14            /* Clear source config register */
#define UT_CNTR                         0xA0            /* Current counter value register */
#define UT_CNTR_PTR                     0xA4            /* Counter compare/pointer register */
#define UT_CHAN_STATUS                  0x114           /* Channel status register */
#define UT_CHAN_INT                     0x118           /* Channel interrupt status register */
#define UT_CHAN_INT_MASK                0x11c           /* Channel interrupt mask register */

/* Global Registers */
#define UT_GLB_CNTR_START               0x0             /* Global counter start control */
#define UT_GLB_CNTR_STOP                0x4             /* Global counter stop control */
#define UT_GLB_CNTR_CLEAR               0x8             /* Global counter clear control */
#define UT_GLB_CNTR_RUNNING             0xc             /* Global running status register */
#define UT_GLB_DRIVER_OEN               0x10            /* Global driver output enable */
#define UT_GLB_DRIVER_CLK_ENABLE        0x20            /* Global clock enable control */

/* Interrupt Definitions */
#define CHAN_INTERRUPT_OVER_FLOW_BIT    7               /* Overflow interrupt bit position */
#define CHAN_INTERRUPT_OVER_FLOW        BIT(7)          /* Overflow interrupt mask */
#define CHAN_INTERRUPT_UNDER_FLOW_BIT   6               /* Underflow interrupt bit position */
#define CHAN_INTERRUPT_UNDER_FLOW       BIT(6)          /* Underflow interrupt mask */
#define CHAN_INTERRUPT_COMPARE_B_BUF2   BIT(5)          /* Compare B Buffer 2 interrupt mask */
#define CHAN_INTERRUPT_COMPARE_B_BUF1   BIT(4)          /* Compare B Buffer 1 interrupt mask */
#define CHAN_INTERRUPT_COMPARE_A_BUF2   BIT(3)          /* Compare A Buffer 2 interrupt mask */
#define CHAN_INTERRUPT_COMPARE_A_BUF1   BIT(2)          /* Compare A Buffer 1 interrupt mask */
#define CHAN_INTERRUPT_CAPTURE_B        BIT(1)          /* Capture B interrupt mask */

/* Capture A / Compare Match interrupt mask */
#define CHAN_INTERRUPT_CAPTURE_A        BIT(0)

#define CHAN_INTERRUPT_ALL_EVENTS       0xFF            /* All interrupts mask */

/* Source Programming */
#define CNTR_SRC1_PGM_EN_BIT            31              /* Source programming enable bit */
#define CNTR_SRC1_PGM_EN                BIT(31)         /* Source programming enable */
#define CNTR_SRC1_PGM_DISABLE           0x0             /* Disable source programming */

/* Compare Value Registers */
#define UT_COMPARE_A                    0xD0
#define UT_COMPARE_B                    0xE0

/* Compare Control Registers */
#define UT_COMPARE_CTRL_A               0x8C            /* Updated from manual */
#define UT_COMPARE_CTRL_B               0x90            /* Updated from manual */

/* Enable Compare Match (COMPARE_EN) */
#define COMPARE_CTRL_DRV_COMPARE_EN     BIT(11)

/* Compare Match (CAPTURE_A) bit in UT_CHAN_INT */
#define CHAN_INTERRUPT_COMPARE_MATCH    BIT(0)

/* Late detection threshold: 100ms (100,000,000 nanoseconds) */
#define UTIMER_LATE_DETECTION_NSEC      100000000

#endif /* __ALIF_UTIMER_REGISTERS_H */
