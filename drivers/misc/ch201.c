// SPDX-License-Identifier: GPL-2.0
/*
 * CH201 Ultrasonic Sensor – transport-only kernel driver
 *
 * All firmware loading and ranging logic lives in userspace (SonicLib).
 * Kernel provides I2C, IRQ, reset, and power management.
 *
 * Copyright (C) 2021-2026 Alif Semiconductor - All Rights Reserved.
 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/wait.h>
#include <linux/poll.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/jiffies.h>
#include <linux/ch201.h>

struct ch201_dev {
	struct i2c_client *client;
	struct miscdevice miscdev;

	struct gpio_desc *reset_gpio;
	struct gpio_desc *prog_gpio;
	struct gpio_desc *int1_gpio;  /* INT1 pin for calibration pulse and interrupt */

	int irq;
	int irq_attached;
	atomic_t irq_pending;
	wait_queue_head_t irq_wq;
};

/* ------------------------------------------------------------------------- */
/* IRQ handling                                                              */
/* ------------------------------------------------------------------------- */

static irqreturn_t ch201_irq_thread(int irq, void *data)
{
	struct ch201_dev *ch201 = data;

	atomic_set(&ch201->irq_pending, 1);
	wake_up_interruptible(&ch201->irq_wq);
	return IRQ_HANDLED;
}

static int ch201_irq_attach(struct ch201_dev *ch201)
{
	int ret;

	if (ch201->irq <= 0 || ch201->irq_attached)
		return 0;

	ret = devm_request_threaded_irq(&ch201->client->dev,
					ch201->irq,
					NULL,
					ch201_irq_thread,
					IRQF_ONESHOT | IRQF_TRIGGER_RISING,
					"ch201",
					ch201);
	if (ret)
		return ret;

	dev_info(&ch201->client->dev, "irq attached\n");
	ch201->irq_attached = 1;
	return 0;
}

static void ch201_irq_detach(struct ch201_dev *ch201)
{
	if (!ch201->irq_attached)
		return;

	dev_info(&ch201->client->dev, "irq freed\n");
	devm_free_irq(&ch201->client->dev, ch201->irq, ch201);
	ch201->irq_attached = 0;
}

/* ------------------------------------------------------------------------- */
/* File operations                                                           */
/* ------------------------------------------------------------------------- */

static int ch201_open(struct inode *inode, struct file *file)
{
	struct miscdevice *mdev = file->private_data;
	struct ch201_dev *ch201 =
		container_of(mdev, struct ch201_dev, miscdev);

	file->private_data = ch201;
	return 0;
}

static long ch201_ioctl(struct file *file,
			unsigned int cmd, unsigned long arg)
{
	struct ch201_dev *ch201 = file->private_data;
	struct ch201_i2c_xfer xfer;
	struct i2c_msg msgs[2];
	int ret;
	u16 timeout_in_ms;

	switch (cmd) {
	case CH201_IOC_I2C_XFER: {
		u8 *kernel_buf;
		u16 offset;

		if (copy_from_user(&xfer,
				   (void __user *)arg,
				   sizeof(xfer)))
			return -EFAULT;

		/* Validate parameters */
		if (!xfer.buf_ptr || xfer.len == 0)
			return -EINVAL;

		/* Allocate kernel buffer for a single chunk */
		kernel_buf = kmalloc(CH201_I2C_WRITE_CHUNK_SIZE, GFP_KERNEL);
		if (!kernel_buf)
			return -ENOMEM;

		/* For writes (large firmware data), copy data from userspace in chunks */
		if (!(xfer.flags & I2C_M_RD)) {
			for (offset = 0; offset < xfer.len; offset += CH201_I2C_WRITE_CHUNK_SIZE) {
				u16 current_chunk = min_t(u16, CH201_I2C_WRITE_CHUNK_SIZE,
							       xfer.len - offset);

				if (copy_from_user(kernel_buf,
						   (void __user *)(uintptr_t)
						   (xfer.buf_ptr + offset),
						   current_chunk)) {
					kfree(kernel_buf);
					return -EFAULT;
				}

				msgs[0].addr  = xfer.addr;
				msgs[0].flags = xfer.flags & ~I2C_M_RD;  /* Force write direction */
				msgs[0].len   = current_chunk;
				msgs[0].buf   = kernel_buf;

				ret = i2c_transfer(ch201->client->adapter, msgs, 1);
				if (ret < 0) {
					kfree(kernel_buf);
					return ret;
				}
			}
		} else {
			/* For reads (typically small), do simple transfer without chunking */
			if (xfer.len > CH201_I2C_WRITE_CHUNK_SIZE) {
				dev_err(&ch201->client->dev, "Implement chunk transfer for read\n");
				kfree(kernel_buf);
				return -EOPNOTSUPP;
			}
			if (xfer.mem_read) {
				if (copy_from_user(kernel_buf,
						   (void __user *)(uintptr_t)(xfer.buf_ptr),
						   CH201_REG_ADDR_SIZE)) {
					kfree(kernel_buf);
					return -EFAULT;
				}
				msgs[0].addr  = xfer.addr;
				/* Force write for first register address */
				msgs[0].flags = xfer.flags & ~I2C_M_RD;
				msgs[0].len   = CH201_REG_ADDR_SIZE;
				msgs[0].buf   = kernel_buf;

				msgs[1].addr  = xfer.addr;
				msgs[1].flags = xfer.flags;  /* I2C_M_RD flag set by userspace */
				msgs[1].len   = xfer.len - CH201_REG_ADDR_SIZE;
				msgs[1].buf   = kernel_buf;

				ret = i2c_transfer(ch201->client->adapter, msgs, 2);
				if (ret < 0) {
					kfree(kernel_buf);
					return ret;
				}
				xfer.len = xfer.len - CH201_REG_ADDR_SIZE;
			} else {
				msgs[0].addr  = xfer.addr;
				msgs[0].flags = xfer.flags;  /* I2C_M_RD flag set by userspace */
				msgs[0].len   = xfer.len;
				msgs[0].buf   = kernel_buf;
				ret = i2c_transfer(ch201->client->adapter, msgs, 1);
				if (ret < 0) {
					kfree(kernel_buf);
					return ret;
				}
			}

			/* Copy read data back to userspace */
			if (copy_to_user((void __user *)(uintptr_t)xfer.buf_ptr,
					 kernel_buf,
					 xfer.len)) {
				kfree(kernel_buf);
				return -EFAULT;
			}
		}

		kfree(kernel_buf);
		return 0;
	}

	case CH201_IOC_RESET_ASSERT:
		if (ch201->reset_gpio)
			gpiod_set_value_cansleep(ch201->reset_gpio, 1);
		return 0;

	case CH201_IOC_RESET_RELEASE:
		if (ch201->reset_gpio)
			gpiod_set_value_cansleep(ch201->reset_gpio, 0);
		return 0;

	case CH201_IOC_PROG_SET:
		if (ch201->prog_gpio)
			gpiod_set_value_cansleep(ch201->prog_gpio, 1);
		return 0;

	case CH201_IOC_PROG_CLEAR:
		if (ch201->prog_gpio)
			gpiod_set_value_cansleep(ch201->prog_gpio, 0);
		return 0;

	case CH201_IOC_WAIT_IRQ:
		long event_ret;

		if (copy_from_user(&timeout_in_ms,
				   (void __user *)arg,
				   sizeof(timeout_in_ms)))
			return -EFAULT;

		event_ret = wait_event_interruptible_timeout(ch201->irq_wq,
							     atomic_read(&ch201->irq_pending),
							     msecs_to_jiffies(timeout_in_ms));

		if (event_ret > 0) {
		    /* event happened */
			atomic_set(&ch201->irq_pending, 0);
			return 1;
		}

		return event_ret;

	case CH201_IOC_INT1_SET:
		ch201_irq_detach(ch201);
		if (ch201->int1_gpio) {
			gpiod_set_value_cansleep(ch201->int1_gpio, 1);
			return 0;
		}
		return -ENODEV;

	case CH201_IOC_INT1_CLEAR:
		ch201_irq_detach(ch201);
		if (ch201->int1_gpio) {
			gpiod_set_value_cansleep(ch201->int1_gpio, 0);
			return 0;
		}
		return -ENODEV;

	case CH201_IOC_INT1_DIR_IN:
		ch201_irq_detach(ch201);
		if (ch201->int1_gpio) {
			gpiod_direction_input(ch201->int1_gpio);
			return 0;
		}
		return -ENODEV;

	case CH201_IOC_INT1_DIR_OUT:
		ch201_irq_detach(ch201);
		if (ch201->int1_gpio) {
			gpiod_direction_output(ch201->int1_gpio, 1);
			return 0;
		}
		return -ENODEV;

	case CH201_IOC_IRQ_ENABLE:
		if (ch201->irq > 0) {
			ch201_irq_attach(ch201);
			return 0;
		}
		return -ENODEV;

	case CH201_IOC_IRQ_DISABLE:
		if (ch201->irq > 0) {
			ch201_irq_detach(ch201);
			return 0;
		}
		return -ENODEV;

	default:
		return -ENOTTY;
	}
}

static __poll_t ch201_poll(struct file *file, poll_table *wait)
{
	struct ch201_dev *ch201 = file->private_data;
	__poll_t mask = 0;

	poll_wait(file, &ch201->irq_wq, wait);

	if (atomic_read(&ch201->irq_pending))
		mask |= POLLIN | POLLRDNORM;

	return mask;
}

static const struct file_operations ch201_fops = {
	.owner          = THIS_MODULE,
	.open           = ch201_open,
	.unlocked_ioctl = ch201_ioctl,
	.poll           = ch201_poll,
	.llseek         = noop_llseek,
};

/* ------------------------------------------------------------------------- */
/* I2C probe/remove                                                          */
/* ------------------------------------------------------------------------- */

static int ch201_probe(struct i2c_client *client)
{
	struct ch201_dev *ch201;
	int ret;

	ch201 = devm_kzalloc(&client->dev, sizeof(*ch201), GFP_KERNEL);
	if (!ch201)
		return -ENOMEM;

	ch201->client = client;
	i2c_set_clientdata(client, ch201);

	init_waitqueue_head(&ch201->irq_wq);
	atomic_set(&ch201->irq_pending, 0);

	ch201->reset_gpio =
		devm_gpiod_get_optional(&client->dev,
					"reset",
					GPIOD_OUT_HIGH);

	ch201->prog_gpio =
		devm_gpiod_get_optional(&client->dev,
					"prog",
					GPIOD_OUT_HIGH);

	ch201->int1_gpio =
		devm_gpiod_get_optional(&client->dev,
					"intr",
					GPIOD_IN);

	ch201->irq = client->irq;
	ch201->irq_attached = 0;
	if (ch201->irq > 0) {
		ret = ch201_irq_attach(ch201);
	if (ret)
		return dev_err_probe(&client->dev, ret, "Failed to request IRQ\n");
	}

	ch201->miscdev.minor = MISC_DYNAMIC_MINOR;
	ch201->miscdev.name =
		devm_kasprintf(&client->dev,
			       GFP_KERNEL,
			       "ch201-%02x",
			       client->addr);
	ch201->miscdev.fops = &ch201_fops;

	ret = misc_register(&ch201->miscdev);
	if (ret)
		return ret;

	dev_info(&client->dev, "CH201 transport driver loaded\n");
	return 0;
}

static void ch201_remove(struct i2c_client *client)
{
	struct ch201_dev *ch201 = i2c_get_clientdata(client);

	misc_deregister(&ch201->miscdev);
}

static const struct of_device_id ch201_of_match[] = {
	{ .compatible = "invensense,ch201" },
	{ }
};
MODULE_DEVICE_TABLE(of, ch201_of_match);

static struct i2c_driver ch201_driver = {
	.driver = {
		.name = "ch201",
		.of_match_table = ch201_of_match,
	},
	.probe = ch201_probe,
	.remove = ch201_remove,
};

module_i2c_driver(ch201_driver);

MODULE_AUTHOR("Aravind Krishnan M <aravind.krishnan@alifsemi.com>");
MODULE_DESCRIPTION("CH201 ultrasonic sensor transport driver");
MODULE_LICENSE("GPL");
