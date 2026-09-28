// SPDX-License-Identifier: GPL-2.0-only
/*
 * Claim the same arm,client MHU0 HE endpoints as pthread_m55_he_mhu0_inloop
 * (rxdb2 / txdb2) so M55-HE can send M55_PERIPH_OFF_REQ without userspace.
 *
 * Does not bind to arm,client (that remains rpmsg_arm_mailbox). Looks up
 * that device and requests the named channels from its DT mbox-names.
 *
 * Copyright (C) 2026 Alif Semiconductor
 */

#include <linux/err.h>
#include <linux/init.h>
#include <linux/mailbox/arm_mhuv2_message.h>
#include <linux/mailbox_client.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/printk.h>
#include <mach/a32_pm.h>

#define M55_PERIPH_OFF_REQ		0xA320FF10U
#define M55_PERIPH_OFF_REQ_ACK		0xA320FF11U

static struct platform_device *arm_client_pdev;
static struct mbox_client he_mhu0_rx_cl;
static struct mbox_client he_mhu0_tx_cl;
static struct mbox_chan *he_mhu0_rx;
static struct mbox_chan *he_mhu0_tx;
static u32 he_mhu0_ack_word = M55_PERIPH_OFF_REQ_ACK;
static struct arm_mhuv2_mbox_msg he_mhu0_ack_msg = {
	.data = &he_mhu0_ack_word,
	.len = sizeof(he_mhu0_ack_word),
};

static void he_mhu0_rx_cb(struct mbox_client *cl, void *mssg)
{
	struct arm_mhuv2_mbox_msg *msg = mssg;
	u32 data;
	int ret;

	if (!msg || !msg->data)
		return;

	data = *(u32 *)msg->data;
	if (data != M55_PERIPH_OFF_REQ) {
		pr_info("rpmsg-arm-mhu-client: rxdb2 ignore 0x%x\n", data);
		return;
	}

	/* IRQ context: tx_block is false — CH_SET only, no sleep. */
	ret = mbox_send_message(he_mhu0_tx, &he_mhu0_ack_msg);
	if (ret < 0)
		pr_err("rpmsg-arm-mhu-client: M55_PERIPH_OFF_REQ_ACK failed: %d\n",
		       ret);
	else
		pr_emerg("rpmsg-arm-mhu-client: M55_PERIPH_OFF_REQ_ACK sent on txdb2\n");

	a32_m55_on_periph_off_req();
}

static int he_mhu0_claim(struct device *dev)
{
	he_mhu0_rx_cl.dev = dev;
	he_mhu0_rx_cl.rx_callback = he_mhu0_rx_cb;
	he_mhu0_rx_cl.tx_block = false;

	he_mhu0_tx_cl.dev = dev;
	he_mhu0_tx_cl.tx_block = false;
	he_mhu0_tx_cl.tx_tout = 0;

	he_mhu0_rx = mbox_request_channel_byname(&he_mhu0_rx_cl, "rxdb2");
	if (IS_ERR(he_mhu0_rx)) {
		dev_err(dev, "rxdb2 (M55-HE MHU0 RX) unavailable: %ld\n",
			PTR_ERR(he_mhu0_rx));
		return PTR_ERR(he_mhu0_rx);
	}

	he_mhu0_tx = mbox_request_channel_byname(&he_mhu0_tx_cl, "txdb2");
	if (IS_ERR(he_mhu0_tx)) {
		dev_err(dev, "txdb2 (M55-HE MHU0 TX) unavailable: %ld\n",
			PTR_ERR(he_mhu0_tx));
		mbox_free_channel(he_mhu0_rx);
		he_mhu0_rx = NULL;
		return PTR_ERR(he_mhu0_tx);
	}

	dev_info(dev, "HE MHU0 claimed (rxdb2/txdb2) for M55_PERIPH_OFF_REQ\n");
	return 0;
}

static void he_mhu0_release(void)
{
	if (he_mhu0_tx) {
		mbox_free_channel(he_mhu0_tx);
		he_mhu0_tx = NULL;
	}
	if (he_mhu0_rx) {
		mbox_free_channel(he_mhu0_rx);
		he_mhu0_rx = NULL;
	}
}

static int rpmsg_arm_mhu_client_probe(struct platform_device *pdev)
{
	struct device_node *np;
	struct platform_device *client;
	int ret;

	np = of_find_compatible_node(NULL, NULL, "arm,client");
	if (!np)
		return -ENODEV;

	client = of_find_device_by_node(np);
	of_node_put(np);
	if (!client)
		return -EPROBE_DEFER;

	ret = he_mhu0_claim(&client->dev);
	if (ret) {
		put_device(&client->dev);
		return ret;
	}

	arm_client_pdev = client;
	return 0;
}

static void rpmsg_arm_mhu_client_remove(struct platform_device *pdev)
{
	he_mhu0_release();
	if (arm_client_pdev) {
		put_device(&arm_client_pdev->dev);
		arm_client_pdev = NULL;
	}
}

static struct platform_driver rpmsg_arm_mhu_client_driver = {
	.driver = {
		.name = "rpmsg-arm-mhu-client",
	},
	.probe = rpmsg_arm_mhu_client_probe,
	.remove = rpmsg_arm_mhu_client_remove,
};

static struct platform_device *rpmsg_arm_mhu_client_pdev;

static int __init rpmsg_arm_mhu_client_init(void)
{
	int ret;

	ret = platform_driver_register(&rpmsg_arm_mhu_client_driver);
	if (ret)
		return ret;

	rpmsg_arm_mhu_client_pdev =
		platform_device_register_simple("rpmsg-arm-mhu-client", -1,
						NULL, 0);
	if (IS_ERR(rpmsg_arm_mhu_client_pdev)) {
		ret = PTR_ERR(rpmsg_arm_mhu_client_pdev);
		platform_driver_unregister(&rpmsg_arm_mhu_client_driver);
		return ret;
	}

	return 0;
}
module_init(rpmsg_arm_mhu_client_init);

static void __exit rpmsg_arm_mhu_client_exit(void)
{
	platform_device_unregister(rpmsg_arm_mhu_client_pdev);
	platform_driver_unregister(&rpmsg_arm_mhu_client_driver);
}
module_exit(rpmsg_arm_mhu_client_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("ARM MHU0 HE client (rxdb2/txdb2 M55_PERIPH_OFF_REQ)");
MODULE_AUTHOR("Alif Semiconductor");
