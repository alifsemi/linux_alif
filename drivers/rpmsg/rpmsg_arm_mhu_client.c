// SPDX-License-Identifier: GPL-2.0-only
/*
 * Probe alif,a32-m55-shutdown and ACK M55_PERIPH_OFF_REQ on non-secure
 * HE MHU pair 1. Pair 0 (txdb2/rxdb2) stays with the arm,client userspace
 * loop; those channels are exclusive.
 *
 * The doorbell values are a software protocol shared with TF-A
 * include/plat/alif/common/alif_mhu.h and the HE sample
 * include/a32_going_off.h. They are not HWRM register fields.
 *
 * HWRM 9.2.1: the sender stays powered until the receiver clears the
 * transfer. The ACK is therefore sent from process context with
 * tx_block, and orderly_poweroff() runs only after that wait succeeds.
 *
 * Copyright (C) 2026 Alif Semiconductor
 */

#include <linux/err.h>
#include <linux/mailbox/arm_mhuv2_message.h>
#include <linux/mailbox_client.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/printk.h>
#include <linux/workqueue.h>
#include <mach/a32_pm.h>

/*
 * Software protocol, kept in sync with TF-A alif_mhu.h and the HE sample.
 * Not defined by the HWRM.
 */
#define M55_PERIPH_OFF_REQ		0xA320FF10U
#define M55_PERIPH_OFF_REQ_ACK		0xA320FF11U

/* Same budget TF-A uses while it waits for A32_GOING_OFF_ACK. */
#define M55_PERIPH_OFF_ACK_TOUT_MS	500

struct alif_m55_shutdown {
	struct device *dev;
	struct mbox_client rx_cl;
	struct mbox_client tx_cl;
	struct mbox_chan *rx;
	struct mbox_chan *tx;
	struct work_struct ack_work;
	u32 ack_word;
	struct arm_mhuv2_mbox_msg ack_msg;
};

static void alif_m55_ack_work(struct work_struct *work)
{
	struct alif_m55_shutdown *shdn =
		container_of(work, struct alif_m55_shutdown, ack_work);
	int ret;

	ret = mbox_send_message(shdn->tx, &shdn->ack_msg);
	if (ret < 0) {
		dev_err(shdn->dev,
			"M55_PERIPH_OFF_REQ_ACK failed: %d (retry allowed)\n",
			ret);
		return;
	}

	dev_emerg(shdn->dev, "M55_PERIPH_OFF_REQ_ACK sent on HE MHU1\n");
	a32_m55_on_periph_off_req();
}

static void alif_m55_rx_cb(struct mbox_client *cl, void *mssg)
{
	struct alif_m55_shutdown *shdn =
		container_of(cl, struct alif_m55_shutdown, rx_cl);
	struct arm_mhuv2_mbox_msg *msg = mssg;
	u32 data;

	if (!msg || !msg->data)
		return;

	data = *(u32 *)msg->data;
	if (data != M55_PERIPH_OFF_REQ) {
		dev_info(shdn->dev, "HE MHU1 ignore 0x%x\n", data);
		return;
	}

	if (!schedule_work(&shdn->ack_work))
		dev_info(shdn->dev, "M55_PERIPH_OFF_REQ already queued\n");
}

static int alif_m55_shutdown_probe(struct platform_device *pdev)
{
	struct alif_m55_shutdown *shdn;
	struct device *dev = &pdev->dev;
	int ret;

	shdn = devm_kzalloc(dev, sizeof(*shdn), GFP_KERNEL);
	if (!shdn)
		return -ENOMEM;

	shdn->dev = dev;
	shdn->ack_word = M55_PERIPH_OFF_REQ_ACK;
	shdn->ack_msg.data = &shdn->ack_word;
	shdn->ack_msg.len = sizeof(shdn->ack_word);
	INIT_WORK(&shdn->ack_work, alif_m55_ack_work);

	shdn->rx_cl.dev = dev;
	shdn->rx_cl.rx_callback = alif_m55_rx_cb;
	shdn->rx_cl.tx_block = false;

	shdn->tx_cl.dev = dev;
	shdn->tx_cl.tx_block = true;
	shdn->tx_cl.tx_tout = M55_PERIPH_OFF_ACK_TOUT_MS;

	shdn->rx = mbox_request_channel_byname(&shdn->rx_cl, "rx");
	if (IS_ERR(shdn->rx)) {
		ret = PTR_ERR(shdn->rx);
		dev_err(dev, "HE MHU1 rx unavailable: %d\n", ret);
		return ret;
	}

	shdn->tx = mbox_request_channel_byname(&shdn->tx_cl, "tx");
	if (IS_ERR(shdn->tx)) {
		ret = PTR_ERR(shdn->tx);
		dev_err(dev, "HE MHU1 tx unavailable: %d\n", ret);
		mbox_free_channel(shdn->rx);
		return ret;
	}

	platform_set_drvdata(pdev, shdn);
	dev_info(dev, "HE MHU1 claimed (tx/rx) for M55_PERIPH_OFF_REQ\n");
	return 0;
}

static void alif_m55_shutdown_remove(struct platform_device *pdev)
{
	struct alif_m55_shutdown *shdn = platform_get_drvdata(pdev);

	cancel_work_sync(&shdn->ack_work);
	mbox_free_channel(shdn->tx);
	mbox_free_channel(shdn->rx);
}

static const struct of_device_id alif_m55_shutdown_of_match[] = {
	{ .compatible = "alif,a32-m55-shutdown" },
	{ }
};
MODULE_DEVICE_TABLE(of, alif_m55_shutdown_of_match);

static struct platform_driver alif_m55_shutdown_driver = {
	.probe = alif_m55_shutdown_probe,
	.remove = alif_m55_shutdown_remove,
	.driver = {
		.name = "alif-a32-m55-shutdown",
		.of_match_table = alif_m55_shutdown_of_match,
	},
};
module_platform_driver(alif_m55_shutdown_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Alif A32 shutdown client on non-secure HE MHU1");
MODULE_AUTHOR("Alif Semiconductor");
