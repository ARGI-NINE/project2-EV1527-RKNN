// SPDX-License-Identifier: GPL-2.0
/*
 * rf433_drv.c — serdev client driver + misc device for project2 RF433 receiver
 *
 * Hardware: STM32 RF capture board connected via UART (9600 8N1, no flow ctrl)
 * Protocol: AA 55 LEN_LO LEN_HI PAYLOAD[len*2] CRC
 *           CRC = XOR-8 over bytes from LEN_LO through end of PAYLOAD
 *
 * Compatible: "project2,rf433-receiver"
 * Target:     Linux 4.19+
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/serdev.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/poll.h>
#include <linux/kfifo.h>
#include <linux/spinlock.h>
#include <linux/timer.h>
#include <linux/ktime.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/sysfs.h>

#include "rf433_ioctl.h"

#define DRV_NAME           "rf433"
#define FRAME_FIFO_DEPTH   16       /* power-of-two not required by kfifo API */
#define BAUD_RATE          9600
#define ONLINE_TIMEOUT_SEC 5
#define ONLINE_CHECK_SEC   2
#define HALF_FRAME_TIMEOUT (HZ)     /* 1 second */

/* ------------------------------------------------------------------ */
/* Parser states (mirrors STM32 rf_protocol.h)                        */
/* ------------------------------------------------------------------ */
enum rf_parse_state {
	RF_ST_SYNC0 = 0,
	RF_ST_SYNC1,
	RF_ST_LEN0,
	RF_ST_LEN1,
	RF_ST_PAYLOAD,
	RF_ST_CRC,
};

#define SYNC0  0xAA
#define SYNC1  0x55

/* ------------------------------------------------------------------ */
/* Per-device context                                                  */
/* ------------------------------------------------------------------ */
struct rf433_priv {
	struct serdev_device *serdev;
	struct miscdevice    misc;

	/* parser state (accessed from serdev rx callback, protected by lock) */
	spinlock_t           lock;
	enum rf_parse_state  state;
	u16                  expected_pulses;
	u16                  payload_idx;
	u8                   payload_buf[RF433_MAX_PULSES * 2];
	u8                   crc_accum;

	/* frame output queue */
	DECLARE_KFIFO_PTR(fifo, struct rf433_frame);
	wait_queue_head_t    rdq;

	/* statistics */
	struct rf433_stats   stats;
	u32                  seq;

	/* online detection */
	struct timer_list    online_timer;
	unsigned long        last_frame_jiffies;
	unsigned long        last_byte_jiffies;
	bool                 online;
};

/* ------------------------------------------------------------------ */
/* Parser helpers                                                      */
/* ------------------------------------------------------------------ */
static void parser_reset(struct rf433_priv *priv)
{
	priv->state           = RF_ST_SYNC0;
	priv->expected_pulses = 0;
	priv->payload_idx     = 0;
	priv->crc_accum       = 0;
}

static void parser_emit_frame(struct rf433_priv *priv)
{
	struct rf433_frame frame;
	u16 i;

	memset(&frame, 0, sizeof(frame));
	frame.timestamp_ns = ktime_get_real_ns();
	frame.pulse_count  = priv->expected_pulses;
	frame.seq          = ++priv->seq;

	for (i = 0; i < priv->expected_pulses; i++) {
		u16 lo = priv->payload_buf[i * 2];
		u16 hi = priv->payload_buf[i * 2 + 1];
		frame.pulse[i] = lo | (hi << 8);
	}

	if (kfifo_is_full(&priv->fifo)) {
		/* Queue-full policy: drop oldest queued frame and keep latest realtime data. */
		kfifo_skip(&priv->fifo);
		priv->stats.drop_cnt++;
	}

	kfifo_in(&priv->fifo, &frame, 1);
	priv->stats.frame_ok++;
	wake_up_interruptible(&priv->rdq);

	priv->last_frame_jiffies = jiffies;
	if (!priv->online)
		priv->online = true;
}

/* Feed one byte into the AA55 protocol state machine.
 * Must be called with priv->lock held. */
static void parser_feed_byte(struct rf433_priv *priv, u8 byte)
{
	priv->last_byte_jiffies = jiffies;

	switch (priv->state) {
	case RF_ST_SYNC0:
		if (byte == SYNC0)
			priv->state = RF_ST_SYNC1;
		break;

	case RF_ST_SYNC1:
		if (byte == SYNC1) {
			priv->state       = RF_ST_LEN0;
			priv->crc_accum   = 0;
			priv->expected_pulses = 0;
			priv->payload_idx = 0;
		} else if (byte == SYNC0) {
			/* stay in SYNC1 — consecutive 0xAA */
		} else {
			priv->state = RF_ST_SYNC0;
		}
		break;

	case RF_ST_LEN0:
		priv->expected_pulses = byte;
		priv->crc_accum       = byte;
		priv->state           = RF_ST_LEN1;
		break;

	case RF_ST_LEN1:
		priv->expected_pulses |= (u16)byte << 8;
		priv->crc_accum       ^= byte;
		if (priv->expected_pulses == 0 ||
		    priv->expected_pulses > RF433_MAX_PULSES) {
			priv->stats.len_err++;
			parser_reset(priv);
		} else {
			priv->state = RF_ST_PAYLOAD;
		}
		break;

	case RF_ST_PAYLOAD:
		priv->payload_buf[priv->payload_idx++] = byte;
		priv->crc_accum ^= byte;
		if (priv->payload_idx >= (u16)(priv->expected_pulses * 2u))
			priv->state = RF_ST_CRC;
		break;

	case RF_ST_CRC:
		if (byte != priv->crc_accum) {
			priv->stats.crc_err++;
			parser_reset(priv);
		} else {
			parser_emit_frame(priv);
			parser_reset(priv);
		}
		break;

	default:
		parser_reset(priv);
		break;
	}
}

/* ------------------------------------------------------------------ */
/* serdev callbacks                                                    */
/* ------------------------------------------------------------------ */
static int rf433_receive_buf(struct serdev_device *serdev,
			     const unsigned char *buf, size_t count)
{
	struct rf433_priv *priv = serdev_device_get_drvdata(serdev);
	unsigned long flags;
	size_t i;

	spin_lock_irqsave(&priv->lock, flags);
	for (i = 0; i < count; i++)
		parser_feed_byte(priv, buf[i]);
	spin_unlock_irqrestore(&priv->lock, flags);

	return count;
}

/*
 * RX-only driver: no pending TX path to drain on wakeup.
 * Keep this callback as an explicit no-op and never re-trigger write wakeups.
 */
static void rf433_write_wakeup_nop(struct serdev_device *serdev)
{
	(void)serdev;
}

static const struct serdev_device_ops rf433_serdev_ops = {
	.receive_buf = rf433_receive_buf,
	.write_wakeup = rf433_write_wakeup_nop,
};

/* ------------------------------------------------------------------ */
/* Online timer                                                        */
/* ------------------------------------------------------------------ */
static void rf433_online_timer_fn(struct timer_list *t)
{
	struct rf433_priv *priv = from_timer(priv, t, online_timer);
	unsigned long flags;
	unsigned long now = jiffies;

	spin_lock_irqsave(&priv->lock, flags);

	/* no frame for 5 seconds → offline */
	if (time_after(now, priv->last_frame_jiffies + ONLINE_TIMEOUT_SEC * HZ))
		priv->online = false;

	/* half-frame timeout: if we are mid-parse and 1 second elapsed since
	 * last byte, reset the state machine to avoid stuck state. */
	if (priv->state != RF_ST_SYNC0 &&
	    time_after(now, priv->last_byte_jiffies + HALF_FRAME_TIMEOUT))
		parser_reset(priv);

	spin_unlock_irqrestore(&priv->lock, flags);

	mod_timer(&priv->online_timer, jiffies + ONLINE_CHECK_SEC * HZ);
}

/* ------------------------------------------------------------------ */
/* misc device file operations                                         */
/* ------------------------------------------------------------------ */
static int rf433_misc_open(struct inode *inode, struct file *filp)
{
	struct rf433_priv *priv =
		container_of(filp->private_data, struct rf433_priv, misc);
	filp->private_data = priv;
	return 0;
}

static ssize_t rf433_misc_read(struct file *filp, char __user *ubuf,
			       size_t count, loff_t *ppos)
{
	struct rf433_priv *priv = filp->private_data;
	struct rf433_frame frame;
	unsigned long flags;
	int ret;

	if (count < sizeof(frame))
		return -EINVAL;

	if (filp->f_flags & O_NONBLOCK) {
		spin_lock_irqsave(&priv->lock, flags);
		ret = kfifo_out(&priv->fifo, &frame, 1);
		spin_unlock_irqrestore(&priv->lock, flags);
		if (ret == 0)
			return -EAGAIN;
	} else {
		ret = wait_event_interruptible(priv->rdq,
					       !kfifo_is_empty(&priv->fifo));
		if (ret)
			return -ERESTARTSYS;
		spin_lock_irqsave(&priv->lock, flags);
		ret = kfifo_out(&priv->fifo, &frame, 1);
		spin_unlock_irqrestore(&priv->lock, flags);
		if (ret == 0)
			return -EIO;
	}

	if (copy_to_user(ubuf, &frame, sizeof(frame)))
		return -EFAULT;

	return sizeof(frame);
}

static __poll_t rf433_misc_poll(struct file *filp, poll_table *wait)
{
	struct rf433_priv *priv = filp->private_data;
	__poll_t mask = 0;

	poll_wait(filp, &priv->rdq, wait);

	if (!kfifo_is_empty(&priv->fifo))
		mask |= EPOLLIN | EPOLLRDNORM;

	return mask;
}

static long rf433_misc_ioctl(struct file *filp, unsigned int cmd,
			     unsigned long arg)
{
	struct rf433_priv *priv = filp->private_data;
	unsigned long flags;

	switch (cmd) {
	case RF433_IOC_GET_STATS: {
		struct rf433_stats st;

		spin_lock_irqsave(&priv->lock, flags);
		st = priv->stats;
		spin_unlock_irqrestore(&priv->lock, flags);

		if (copy_to_user((void __user *)arg, &st, sizeof(st)))
			return -EFAULT;
		return 0;
	}
	case RF433_IOC_CLR_STATS:
		spin_lock_irqsave(&priv->lock, flags);
		memset(&priv->stats, 0, sizeof(priv->stats));
		spin_unlock_irqrestore(&priv->lock, flags);
		return 0;

	case RF433_IOC_GET_STATUS: {
		struct rf433_status status;

		memset(&status, 0, sizeof(status));
		spin_lock_irqsave(&priv->lock, flags);
		status.online         = priv->online ? 1 : 0;
		status.seq            = priv->seq;
		status.queue_depth    = kfifo_len(&priv->fifo);
		status.queue_capacity = FRAME_FIFO_DEPTH;
		spin_unlock_irqrestore(&priv->lock, flags);

		if (copy_to_user((void __user *)arg, &status, sizeof(status)))
			return -EFAULT;
		return 0;
	}
	case RF433_IOC_FLUSH_QUEUE:
		spin_lock_irqsave(&priv->lock, flags);
		kfifo_reset(&priv->fifo);
		spin_unlock_irqrestore(&priv->lock, flags);
		return 0;

	default:
		return -ENOTTY;
	}
}

static const struct file_operations rf433_misc_fops = {
	.owner          = THIS_MODULE,
	.open           = rf433_misc_open,
	.read           = rf433_misc_read,
	.poll           = rf433_misc_poll,
	.unlocked_ioctl = rf433_misc_ioctl,
	.compat_ioctl   = compat_ptr_ioctl,
	.llseek         = no_llseek,
};
/* sysfs attributes                                                    */
/* ------------------------------------------------------------------ */
#define RF433_SYSFS_RO(_name, _field, _fmt)                            \
static ssize_t _name##_show(struct device *dev,                        \
			    struct device_attribute *attr, char *buf)   \
{                                                                      \
	struct rf433_priv *priv = container_of(                        \
		(struct miscdevice *)dev_get_drvdata(dev),             \
		struct rf433_priv, misc);                              \
	unsigned long flags;                                           \
	typeof(priv->_field) val;                                      \
	spin_lock_irqsave(&priv->lock, flags);                        \
	val = priv->_field;                                            \
	spin_unlock_irqrestore(&priv->lock, flags);                   \
	return sysfs_emit(buf, _fmt "\n", val);                        \
}                                                                      \
static DEVICE_ATTR_RO(_name)

RF433_SYSFS_RO(frame_ok, stats.frame_ok, "%llu");
RF433_SYSFS_RO(crc_err,  stats.crc_err,  "%llu");
RF433_SYSFS_RO(len_err,  stats.len_err,  "%llu");
RF433_SYSFS_RO(drop_cnt, stats.drop_cnt, "%llu");
RF433_SYSFS_RO(seq,      seq,            "%u");

static ssize_t online_show(struct device *dev,
			   struct device_attribute *attr, char *buf)
{
	struct rf433_priv *priv = container_of(
		(struct miscdevice *)dev_get_drvdata(dev),
		struct rf433_priv, misc);
	unsigned long flags;
	bool val;

	spin_lock_irqsave(&priv->lock, flags);
	val = priv->online;
	spin_unlock_irqrestore(&priv->lock, flags);

	return sysfs_emit(buf, "%d\n", val ? 1 : 0);
}
static DEVICE_ATTR_RO(online);

static struct attribute *rf433_attrs[] = {
	&dev_attr_frame_ok.attr,
	&dev_attr_crc_err.attr,
	&dev_attr_len_err.attr,
	&dev_attr_drop_cnt.attr,
	&dev_attr_online.attr,
	&dev_attr_seq.attr,
	NULL,
};
ATTRIBUTE_GROUPS(rf433);

/* ------------------------------------------------------------------ */
/* serdev probe / remove                                               */
/* ------------------------------------------------------------------ */
static int rf433_probe(struct serdev_device *serdev)
{
	struct device *dev = &serdev->dev;
	struct rf433_priv *priv;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->serdev = serdev;
	spin_lock_init(&priv->lock);
	init_waitqueue_head(&priv->rdq);
	parser_reset(priv);
	priv->last_frame_jiffies = jiffies;
	priv->last_byte_jiffies  = jiffies;

	ret = kfifo_alloc(&priv->fifo, FRAME_FIFO_DEPTH, GFP_KERNEL);
	if (ret) {
		dev_err(dev, "kfifo_alloc failed: %d\n", ret);
		return ret;
	}

	serdev_device_set_drvdata(serdev, priv);
	serdev_device_set_client_ops(serdev, &rf433_serdev_ops);

	ret = serdev_device_open(serdev);
	if (ret) {
		dev_err(dev, "serdev_device_open failed: %d\n", ret);
		goto err_fifo;
	}

	serdev_device_set_baudrate(serdev, BAUD_RATE);
	serdev_device_set_flow_control(serdev, false);
	serdev_device_set_parity(serdev, SERDEV_PARITY_NONE);

	/* misc device */
	priv->misc.minor  = MISC_DYNAMIC_MINOR;
	priv->misc.name   = DRV_NAME;
	priv->misc.fops   = &rf433_misc_fops;
	priv->misc.parent = dev;
	priv->misc.groups = rf433_groups;

	ret = misc_register(&priv->misc);
	if (ret) {
		dev_err(dev, "misc_register failed: %d\n", ret);
		goto err_serdev;
	}

	/* online check timer */
	timer_setup(&priv->online_timer, rf433_online_timer_fn, 0);
	mod_timer(&priv->online_timer, jiffies + ONLINE_CHECK_SEC * HZ);

	dev_info(dev, "rf433 serdev driver probed (%d baud)\n", BAUD_RATE);
	return 0;

err_serdev:
	serdev_device_close(serdev);
err_fifo:
	kfifo_free(&priv->fifo);
	return ret;
}

static void rf433_remove(struct serdev_device *serdev)
{
	struct rf433_priv *priv = serdev_device_get_drvdata(serdev);

	del_timer_sync(&priv->online_timer);
	misc_deregister(&priv->misc);
	serdev_device_close(serdev);
	kfifo_free(&priv->fifo);

	dev_info(&serdev->dev, "rf433 serdev driver removed\n");
}

/* ------------------------------------------------------------------ */
/* Device-tree match                                                   */
/* ------------------------------------------------------------------ */
static const struct of_device_id rf433_of_match[] = {
	{ .compatible = "project2,rf433-receiver" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, rf433_of_match);

static struct serdev_device_driver rf433_driver = {
	.driver = {
		.name           = DRV_NAME,
		.of_match_table = rf433_of_match,
	},
	.probe  = rf433_probe,
	.remove = rf433_remove,
};
module_serdev_device_driver(rf433_driver);

MODULE_AUTHOR("project2");
MODULE_DESCRIPTION("RF433 serdev receiver driver with AA55 frame parser");
MODULE_LICENSE("GPL v2");
