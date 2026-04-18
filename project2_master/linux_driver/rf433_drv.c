/*
 * rf433_drv.c
 *
 * DEPRECATED sample (history-only): this file is not part of the master runtime
 * path and is not built by CMake targets in project2_master.
 *
 * Boundary:
 * 1) rf_gateway production path is userspace termios on /dev/ttyS9.
 * 2) This sample creates /dev/rf433 and does not register as RK3568 UART9
 *    vendor driver (no DT probe/pinctrl/clock/DMA ownership).
 * 3) Therefore it cannot replace the vendor tty/UART driver that provides
 *    /dev/ttyS9. Keep this file for reference only.
 */

#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/uaccess.h>
#include <linux/wait.h>

#define DEV_NAME "rf433"
#define RING_SIZE 4096
#define RF433_UART_BRIDGE_BAUD 9600

struct rf_ring {
    uint8_t buf[RING_SIZE];
    int head;
    int tail;
};

struct rf433_dev {
    struct cdev cdev;
    dev_t devno;
    struct class *cls;
    struct device *dev;
    struct rf_ring ring;
    spinlock_t lock;
    wait_queue_head_t rdq;
};

static struct rf433_dev *g_dev;

static int ring_count(const struct rf_ring *r) {
    return (r->head - r->tail + RING_SIZE) % RING_SIZE;
}

static int ring_push(struct rf_ring *r, uint8_t b) {
    const int next = (r->head + 1) % RING_SIZE;
    if (next == r->tail) {
        return -1;
    }
    r->buf[r->head] = b;
    r->head = next;
    return 0;
}

static int ring_pop(struct rf_ring *r, uint8_t *b) {
    if (r->tail == r->head) {
        return -1;
    }
    *b = r->buf[r->tail];
    r->tail = (r->tail + 1) % RING_SIZE;
    return 0;
}

static int rf433_open(struct inode *inode, struct file *filp) {
    filp->private_data = g_dev;
    return 0;
}

static ssize_t rf433_read(struct file *filp, char __user *buf, size_t len, loff_t *off) {
    struct rf433_dev *d = filp->private_data;
    size_t copied = 0;
    unsigned long flags;
    uint8_t *kbuf;
    (void)off;

    if (d == NULL) {
        return -ENODEV;
    }
    if (len == 0) {
        return 0;
    }
    if (len > RING_SIZE)
        len = RING_SIZE;

    if (wait_event_interruptible(d->rdq, ring_count(&d->ring) > 0)) {
        return -ERESTARTSYS;
    }

    kbuf = kmalloc(len, GFP_KERNEL);
    if (!kbuf)
        return -ENOMEM;

    /* Hold spinlock for the entire ring drain to avoid TOCTOU race with IRQ */
    spin_lock_irqsave(&d->lock, flags);
    while (copied < len) {
        uint8_t b;
        if (ring_pop(&d->ring, &b) != 0)
            break;
        kbuf[copied++] = b;
    }
    spin_unlock_irqrestore(&d->lock, flags);

    if (copy_to_user(buf, kbuf, copied)) {
        kfree(kbuf);
        return -EFAULT;
    }
    kfree(kbuf);
    return (ssize_t)copied;
}

static unsigned int rf433_poll(struct file *filp, poll_table *wait) {
    struct rf433_dev *d = filp->private_data;
    unsigned int mask = 0;
    unsigned long flags;
    if (d == NULL) {
        return POLLERR;
    }
    poll_wait(filp, &d->rdq, wait);
    spin_lock_irqsave(&d->lock, flags);
    if (ring_count(&d->ring) > 0) {
        mask |= POLLIN | POLLRDNORM;
    }
    spin_unlock_irqrestore(&d->lock, flags);
    return mask;
}

void rf_uart_rx_irq(uint8_t byte) {
    unsigned long flags;
    if (g_dev == NULL) {
        return;
    }
    spin_lock_irqsave(&g_dev->lock, flags);
    (void)ring_push(&g_dev->ring, byte);
    spin_unlock_irqrestore(&g_dev->lock, flags);
    wake_up_interruptible(&g_dev->rdq);
}
EXPORT_SYMBOL(rf_uart_rx_irq);

static const struct file_operations rf433_fops = {
    .owner = THIS_MODULE,
    .open = rf433_open,
    .read = rf433_read,
    .poll = rf433_poll,
};

/*
 * Integration note: This driver uses module_init/module_exit for standalone
 * char device creation. For proper RK3568 device-tree integration, convert to
 * platform_driver with probe/remove callbacks. The UART bridge callback
 * rf_uart_rx_irq() is exported via EXPORT_SYMBOL for cross-module use.
 */
static int __init rf433_init(void) {
    int ret = 0;
    g_dev = kzalloc(sizeof(*g_dev), GFP_KERNEL);
    if (g_dev == NULL) {
        return -ENOMEM;
    }

    spin_lock_init(&g_dev->lock);
    init_waitqueue_head(&g_dev->rdq);

    ret = alloc_chrdev_region(&g_dev->devno, 0, 1, DEV_NAME);
    if (ret != 0) {
        goto fail_alloc;
    }

    cdev_init(&g_dev->cdev, &rf433_fops);
    ret = cdev_add(&g_dev->cdev, g_dev->devno, 1);
    if (ret != 0) {
        goto fail_cdev;
    }

    g_dev->cls = class_create(THIS_MODULE, DEV_NAME);
    if (IS_ERR(g_dev->cls)) {
        ret = PTR_ERR(g_dev->cls);
        goto fail_class;
    }
    g_dev->dev = device_create(g_dev->cls, NULL, g_dev->devno, NULL, DEV_NAME);
    if (IS_ERR(g_dev->dev)) {
        ret = PTR_ERR(g_dev->dev);
        goto fail_device;
    }

    pr_info("rf433_drv loaded: /dev/%s\n", DEV_NAME);
    return 0;

fail_device:
    class_destroy(g_dev->cls);
fail_class:
    cdev_del(&g_dev->cdev);
fail_cdev:
    unregister_chrdev_region(g_dev->devno, 1);
fail_alloc:
    kfree(g_dev);
    g_dev = NULL;
    return ret;
}

static void __exit rf433_exit(void) {
    if (g_dev == NULL) {
        return;
    }
    device_destroy(g_dev->cls, g_dev->devno);
    if (g_dev->cls)
        class_destroy(g_dev->cls);
    cdev_del(&g_dev->cdev);
    unregister_chrdev_region(g_dev->devno, 1);
    {
        struct rf433_dev *dev = g_dev;
        g_dev = NULL;
        smp_mb(); /* ensure g_dev=NULL visible before freeing memory */
        kfree(dev);
    }
    pr_info("rf433_drv unloaded\n");
}

module_init(rf433_init);
module_exit(rf433_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("project2");
MODULE_DESCRIPTION("433MHz RF UART bridge character driver");
