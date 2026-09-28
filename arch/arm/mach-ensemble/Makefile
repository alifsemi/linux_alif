# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the linux kernel.
#

ccflags-y := -I$(srctree)/$(src)/include \
	-I$(srctree)/arch/arm/plat-ensemble/include

# Common support
obj-y := ensemble.o sdhci-alif-reset.o adc_dac6.o a32_m55_shutdown.o
obj-$(CONFIG_HOTPLUG_CPU) += a32_secondary.o
