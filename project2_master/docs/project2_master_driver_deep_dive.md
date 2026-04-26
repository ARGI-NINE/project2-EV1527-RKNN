# project2_master RF433 驱动深读

本文只解释 `linux_driver/rf433_drv.c` 和 `linux_driver/rf433_ioctl.h`。  
写法上尽量贴近 `rknpu_best/.../docs`：先贴代码，再解释。你要抓住的点是，这不是一份“文件概览”，而是一份按调用链往下拆的驱动读法。

不讲 `pc sim`，不讲 vendor，不讲生成文件，也不改其他源码。

## 0. 快速阅读地图

如果你是带着“先把用户态链路读通”的目标进来，不要按章节编号硬啃，先顺着这几段看：

1. 先看 `2` 和 `4`，把 `probe()` 里资源是怎么挂起来的、核心私有对象里有哪些状态先抓住
2. 再看 `5` 和 `6`，把协议解析是怎么把串口字节变成一帧的抓住
3. 然后看 `7` 和 `8`，把 `/dev/rf433` 的 `read/poll/ioctl` 契约抓住
4. 最后看 `9`、`10`、`11`，把在线状态、sysfs 观测面和退出收尾串起来

你要抓住的点是：这份驱动不是“文件顺序”，而是“调用链顺序”。
- 生产者链路是 `serdev receive_buf -> parser -> kfifo -> wake_up`
- 消费者链路是 `read()/poll()/ioctl()/sysfs`
- 收尾链路是 `del_timer_sync -> misc_deregister -> serdev_device_close -> kfifo_free`

不是先背接口名，而是先背这条链。

## 1. 先看整体位置

这份驱动在系统里的角色很明确：

- 它是一个 `serdev client driver`
- 它同时注册了一个 `misc device`
- 它在内核里完成 `AA55 + LEN + PAYLOAD + CRC` 的协议解析
- 它把解析后的帧通过 `/dev/rf433` 暴露给用户态
- 它还提供 `ioctl()`、`poll()` 和一组 sysfs 只读属性

用户态看到的不是原始串口字节流，而是已经拼好的 `struct rf433_frame`。  
不是“字节来了就原样往外抛”，而是“先在内核里拼成一帧，再按结构体交给用户态”。

## 2. 驱动入口：从模块加载到 probe

先看驱动注册和设备树匹配：

```c
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
```

这段的作用是：

- 让设备树里 `compatible = "project2,rf433-receiver"` 的节点匹配到这个驱动
- 让 `serdev` 框架在匹配成功后回调 `rf433_probe()`
- 让模块加载和卸载时自动完成 `probe/remove` 的绑定

你要抓住的点是：

- 这个驱动不是普通 `platform_driver`
- 它不是自己去扫串口，而是靠 `serdev` 框架把底层 UART 设备交给它
- `probe()` 才是整个初始化链路的起点

再看 `probe()` 的真实顺序：

```c
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
	if (ret)
		return ret;

	serdev_device_set_drvdata(serdev, priv);
	serdev_device_set_client_ops(serdev, &rf433_serdev_ops);

	ret = serdev_device_open(serdev);
	if (ret)
		goto err_fifo;

	serdev_device_set_baudrate(serdev, BAUD_RATE);
	serdev_device_set_flow_control(serdev, false);
	serdev_device_set_parity(serdev, SERDEV_PARITY_NONE);

	priv->misc.minor  = MISC_DYNAMIC_MINOR;
	priv->misc.name   = DRV_NAME;
	priv->misc.fops   = &rf433_misc_fops;
	priv->misc.parent = dev;
	priv->misc.groups = rf433_groups;

	ret = misc_register(&priv->misc);
	if (ret)
		goto err_serdev;

	timer_setup(&priv->online_timer, rf433_online_timer_fn, 0);
	mod_timer(&priv->online_timer, jiffies + ONLINE_CHECK_SEC * HZ);

	return 0;

err_serdev:
	serdev_device_close(serdev);
err_fifo:
	kfifo_free(&priv->fifo);
	return ret;
}
```

这段的作用是：

1. 分配并初始化每设备私有对象 `struct rf433_priv`
2. 建立 `spinlock` 和 `waitqueue`
3. 把解析状态机复位到初始态
4. 分配帧队列 `kfifo`
5. 绑定 `serdev` 回调
6. 打开串口并设置参数
7. 注册 misc 设备节点
8. 启动在线检测定时器

你要抓住的点是：

- 初始化顺序是有意设计的，不是随便写的
- `kfifo_alloc()` 在 `misc_register()` 前面，因为用户态入口挂出来之前，队列必须已经可用
- `serdev_device_open()` 在串口参数设置链路上是关键一步，后面才有硬件数据进来
- `misc_register()` 成功后，系统才会出现 `/dev/rf433`

## 3. 设备节点怎么出来

`miscdevice` 的配置就在 `probe()` 里：

```c
priv->misc.minor  = MISC_DYNAMIC_MINOR;
priv->misc.name   = DRV_NAME;
priv->misc.fops   = &rf433_misc_fops;
priv->misc.parent = dev;
priv->misc.groups = rf433_groups;
```

这里的含义是：

- `minor = MISC_DYNAMIC_MINOR`：由内核动态分配次设备号
- `name = "rf433"`：最终节点名就是 `/dev/rf433`
- `fops`：用户态打开这个节点后，走哪组文件操作函数
- `parent`：把 misc 设备挂在对应的 `serdev` 设备下面
- `groups`：把 sysfs 属性组一起挂出去

这段的作用是：

- 把驱动的“数据入口”从内核对象变成一个标准字符设备节点
- 让用户态只需要 `open("/dev/rf433")` 就能进入驱动

不是“驱动自己开了一个特殊接口”，而是“借 misc device 给自己挂了一个标准字符设备外壳”。

## 4. 核心对象：状态、队列、统计、在线状态

先看私有上下文：

```c
struct rf433_priv {
	struct serdev_device *serdev;
	struct miscdevice    misc;

	spinlock_t           lock;
	enum rf_parse_state  state;
	u16                  expected_pulses;
	u16                  payload_idx;
	u8                   payload_buf[RF433_MAX_PULSES * 2];
	u8                   crc_accum;

	DECLARE_KFIFO_PTR(fifo, struct rf433_frame);
	wait_queue_head_t    rdq;

	struct rf433_stats   stats;
	u32                  seq;

	struct timer_list    online_timer;
	unsigned long        last_frame_jiffies;
	unsigned long        last_byte_jiffies;
	bool                 online;
};
```

这段的作用是把驱动运行时需要的东西一次性收在一个对象里。

你要抓住的点是：

- `state / expected_pulses / payload_idx / payload_buf / crc_accum` 组成了解析状态机
- `fifo / rdq` 组成了用户态读队列
- `stats / seq` 组成了运行统计
- `last_frame_jiffies / last_byte_jiffies / online_timer / online` 组成了在线检测

### 1. 资源生命周期总表

这段的作用是把 `probe()` 里几个资源的“创建、绑定、释放顺序”一次看清楚。你要抓住的点是：**先准备生产/消费能力，再把设备节点挂出去，最后按相反顺序收尾**。

| 资源 | 创建 / 绑定 | 释放 / 回滚 | 你要记住的顺序 |
|---|---|---|---|
| `serdev` | `serdev_device_set_drvdata()`，`serdev_device_set_client_ops()`，`serdev_device_open()` | `serdev_device_close()` | 先把串口生产者接上，再谈用户态出口 |
| `miscdevice` | 填 `minor/name/fops/parent/groups` 后 `misc_register()` | `misc_deregister()` | 节点只在注册成功后出现 |
| `kfifo` | `kfifo_alloc()` | `kfifo_free()` | 先有队列，后有 `/dev/rf433` |
| `timer` | `timer_setup()`，`mod_timer()` | `del_timer_sync()` | 先停定时器，再拆对象 |
| `waitqueue` | `init_waitqueue_head()` | 无显式释放 | 这是睡眠/唤醒契约，不是独立资源 |

更直白一点：
- `kfifo` 和 `waitqueue` 是“用户态读取面”的底座
- `serdev` 是“硬件输入面”的底座
- `miscdevice` 是“用户态看到的入口”
- `timer` 是“在线状态和半帧超时”的巡检器

不是“注册完再慢慢补”，而是“队列、状态机、输入通道先准备好，最后一次性挂出设备节点”。

### 4.1 `struct rf433_frame`

```c
struct rf433_frame {
	__u64 timestamp_ns;
	__u16 pulse_count;
	__u16 reserved;
	__u32 seq;
	__u16 pulse[RF433_MAX_PULSES];
};
```

这段的作用是定义 `read()` 给用户态返回的一帧。

字段含义如下：

| 字段 | 含义 |
|---|---|
| `timestamp_ns` | 帧完成时的实时时间戳 |
| `pulse_count` | 本帧有效脉冲数量 |
| `reserved` | 填充位，当前固定为 0 |
| `seq` | 单调递增的帧序号 |
| `pulse[]` | 解出来的脉冲数组，每个元素是一个 `u16` |

你要抓住的点是：

- 用户态拿到的是“结构化帧”，不是裸串口数据
- `pulse[]` 的元素不是字节，而是把 payload 的两个字节拼成一个 `u16`

### 4.2 `struct rf433_stats`

```c
struct rf433_stats {
	__u64 frame_ok;
	__u64 crc_err;
	__u64 len_err;
	__u64 drop_cnt;
};
```

这段的作用是记录驱动运行统计。

| 字段 | 含义 |
|---|---|
| `frame_ok` | 成功解析并入队的帧数 |
| `crc_err` | CRC 不匹配次数 |
| `len_err` | 长度非法次数 |
| `drop_cnt` | 队列满时丢弃旧帧的次数 |

你要抓住的点是：

- 这些统计全部是在内核里维护的
- `GET_STATS` 只是把快照拷给用户态

### 4.3 `struct rf433_status`

```c
struct rf433_status {
	__u8  online;
	__u8  reserved[3];
	__u32 seq;
	__u32 queue_depth;
	__u32 queue_capacity;
};
```

这段的作用是对外暴露“当前驱动状态”。

| 字段 | 含义 |
|---|---|
| `online` | 当前是否还认为硬件在线 |
| `seq` | 当前帧序号 |
| `queue_depth` | 队列中当前有多少帧 |
| `queue_capacity` | 队列容量 |

你要抓住的点是：

- `queue_depth` 不是字节数，而是帧数
- `queue_capacity` 固定来自 `FRAME_FIFO_DEPTH`

## 5. AA55/LEN/PAYLOAD/CRC 解析状态机

先看状态机定义：

```c
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
```

这段的作用是把协议拆成几个严格的阶段。

你要抓住的点是：

- 这是一个按字节推进的同步状态机
- 入口是 `0xAA 0x55`
- 后面依次是长度、payload、CRC

### 5.1 解析前的复位

```c
static void parser_reset(struct rf433_priv *priv)
{
	priv->state           = RF_ST_SYNC0;
	priv->expected_pulses = 0;
	priv->payload_idx     = 0;
	priv->crc_accum       = 0;
}
```

这段的作用是把解析器打回起点。

你要抓住的点是：

- 一旦长度非法、CRC 错误，或者定时器判断半帧超时，都会回到这里
- 不是“继续硬解”，而是“直接丢弃当前半帧，等待下一次同步头”

### 5.2 按字节喂给状态机

```c
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
			/* stay in SYNC1 */
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
```

这段的作用是把协议按字节一层层拆开。

你要抓住的点是：

1. `SYNC0` 只认 `0xAA`
2. `SYNC1` 只认 `0x55`
3. 长度字段是小端两字节，先低后高
4. CRC 是从 `LEN_LO` 开始一路 XOR 到 payload 结束
5. payload 长度是 `expected_pulses * 2` 个字节
6. 一旦长度非法或 CRC 错，立刻复位

不是“收到 payload 就结束”，而是“收到完整 payload 后还要再验一个 CRC 字节”。

### 5.3 把 payload 变成用户态帧

```c
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
```

这段的作用是把协议帧转换成用户态可读的结构体帧。

你要抓住的点是：

- `payload_buf` 里是原始字节
- `frame.pulse[i]` 里是 `u16` 脉冲值
- 队列满时不是阻塞生产者，而是丢最旧帧，保最新实时数据
- `wake_up_interruptible()` 是把阻塞在 `read()` 上的进程叫醒
- `online` 在这里被置为真，说明一旦成功收帧，驱动就认为硬件在线

## 6. 硬件侧数据怎么进来

真正的输入入口是 `serdev` 回调：

```c
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
```

这段的作用是：

- `serdev` 收到 UART 数据后回调这个函数
- 函数逐字节把数据喂给状态机
- 返回值是 `count`，表示这批字节都被驱动消费了

你要抓住的点是：

- 硬件数据不是通过 `write()` 进入驱动
- 它是从串口 RX 路径进来的
- 整个解析过程在自旋锁保护下运行

不是用户态“发命令写进去”，而是硬件“推字节上来，驱动被动消费”。

### 6.1 串口回调表

```c
static void rf433_write_wakeup_nop(struct serdev_device *serdev)
{
	(void)serdev;
}

static const struct serdev_device_ops rf433_serdev_ops = {
	.receive_buf = rf433_receive_buf,
	.write_wakeup = rf433_write_wakeup_nop,
};
```

这段的作用是说明驱动是 RX-only 的。

你要抓住的点是：

- 驱动没有主动发送路径
- `write_wakeup` 被显式写成 no-op
- 这不是“忘了实现”，而是“刻意不提供 TX 语义”

## 7. file_operations：用户态怎么进来

先看文件操作表：

```c
static const struct file_operations rf433_misc_fops = {
	.owner          = THIS_MODULE,
	.open           = rf433_misc_open,
	.read           = rf433_misc_read,
	.poll           = rf433_misc_poll,
	.unlocked_ioctl = rf433_misc_ioctl,
	.compat_ioctl   = compat_ptr_ioctl,
	.llseek         = no_llseek,
};
```

这段的作用是把 `/dev/rf433` 的行为钉死。

你要抓住的点是：

- 有 `open/read/poll/ioctl`
- 没有 `write`
- 没有 seek
- 32 位兼容路径用的是 `compat_ptr_ioctl`

### 7.1 `open()`

```c
static int rf433_misc_open(struct inode *inode, struct file *filp)
{
	struct rf433_priv *priv =
		container_of(filp->private_data, struct rf433_priv, misc);
	filp->private_data = priv;
	return 0;
}
```

这段的作用是把 `file->private_data` 从 `miscdevice` 指针换成 `struct rf433_priv *`。

你要抓住的点是：

- 后续 `read()`、`poll()`、`ioctl()` 都直接从 `filp->private_data` 拿私有上下文
- 这是 `misc device` 常见写法

### 7.2 `read()`

```c
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
```

这段的作用是把队列里的一帧取出来拷给用户态。

读法要分成两条路径看：

1. 非阻塞路径
2. 阻塞路径

#### 非阻塞路径

- `O_NONBLOCK` 打开时，先直接尝试 `kfifo_out()`
- 队列空就返回 `-EAGAIN`

#### 阻塞路径

- 先在 `rdq` 上睡眠，等队列非空
- 被信号打断就返回 `-ERESTARTSYS`
- 醒来后再取一帧
- 如果醒来后队列莫名其妙又空了，返回 `-EIO`

你要抓住的点是：

- `read()` 是“按帧读”，不是“按字节读”
- 传入的 `count` 必须至少能放下一个 `struct rf433_frame`
- 成功时返回值固定是 `sizeof(struct rf433_frame)`
- `ppos` 没有被使用

不是“读到多少返回多少”，而是“要么给你整帧，要么报错”。

### 7.3 `poll()`

```c
static __poll_t rf433_misc_poll(struct file *filp, poll_table *wait)
{
	struct rf433_priv *priv = filp->private_data;
	__poll_t mask = 0;

	poll_wait(filp, &priv->rdq, wait);

	if (!kfifo_is_empty(&priv->fifo))
		mask |= EPOLLIN | EPOLLRDNORM;

	return mask;
}
```

这段的作用是让用户态可以用 `poll()` / `epoll()` 等待帧到达。

你要抓住的点是：

- 只要队列非空，就返回可读事件
- 这和 `read()` 的等待队列是同一套 `rdq`

### 2. 并发与唤醒契约

这段的作用是把“谁保护谁、谁睡、谁叫醒谁”说清楚。你要抓住的点是：**`spinlock` 保护状态，`waitqueue` 负责睡眠，`wake_up_interruptible()` 负责把消费者叫醒**。

- `spin_lock_irqsave()` 包住 `parser_feed_byte()` 和 `parser_emit_frame()`，保护的是解析状态、统计字段、队列和在线状态这些共享数据
- `read()` 阻塞路径不是自旋等，而是 `wait_event_interruptible(priv->rdq, !kfifo_is_empty(...))`
- `poll()` 也是挂同一个 `rdq`，所以 `read()` 和 `poll()` 实际上共享同一套唤醒面
- `parser_emit_frame()` 里先 `kfifo_in()`，再 `wake_up_interruptible(&priv->rdq)`，这就是“先放数据，再叫醒人”
- `read()` 的非阻塞路径不睡眠，队列空就直接 `-EAGAIN`
- `read()` 的阻塞路径被信号打断时返回 `-ERESTARTSYS`

不是 timer 去叫醒 `read()`，而是“解析到完整帧以后”才叫醒等待者；timer 只负责在线判定和半帧超时修复，不负责数据就绪。

你还要额外记住一条：
- 这个 `wake_up_interruptible()` 既会唤醒阻塞的 `read()`，也会唤醒通过 `poll_wait()` 挂在同一个 waitqueue 上的等待者

所以这里真正的并发契约不是“谁读谁写”，而是“生产者把帧放进 fifo，消费者在 rdq 上等帧”。

## 8. ioctl ABI：每个命令到底干什么

头文件里的 ioctl ABI 很直接：

```c
#define RF433_IOC_MAGIC  'R'

#define RF433_IOC_GET_STATS    _IOR(RF433_IOC_MAGIC, 0x10, struct rf433_stats)
#define RF433_IOC_CLR_STATS    _IO(RF433_IOC_MAGIC,  0x11)
#define RF433_IOC_GET_STATUS   _IOR(RF433_IOC_MAGIC, 0x12, struct rf433_status)
#define RF433_IOC_FLUSH_QUEUE  _IO(RF433_IOC_MAGIC,  0x13)
```

这段的作用是定义用户态和内核之间的固定控制协议。

你要抓住的点是：

- 魔数是 `'R'`
- 命令号固定是 `0x10` 到 `0x13`
- 其中两个是读回结构体，两个是无参数命令

### 8.1 ioctl 一览表

| 命令 | 宏定义 | 用户态参数 | 驱动行为 | 返回值 |
|---|---|---|---|---|
| `RF433_IOC_GET_STATS` | `_IOR('R', 0x10, struct rf433_stats)` | `struct rf433_stats *` | 拷贝 `frame_ok/crc_err/len_err/drop_cnt` 快照 | 成功返回 0，失败返回 `-EFAULT` |
| `RF433_IOC_CLR_STATS` | `_IO('R', 0x11)` | 无 | 清零 `stats` | 成功返回 0 |
| `RF433_IOC_GET_STATUS` | `_IOR('R', 0x12, struct rf433_status)` | `struct rf433_status *` | 拷贝 `online/seq/queue_depth/queue_capacity` 快照 | 成功返回 0，失败返回 `-EFAULT` |
| `RF433_IOC_FLUSH_QUEUE` | `_IO('R', 0x13)` | 无 | 清空 `kfifo` | 成功返回 0 |

### 8.2 ioctl 实现

```c
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
```

这段的作用是把驱动状态、统计和队列控制暴露给用户态。

你要抓住的点是：

- 所有受保护字段都是在自旋锁内快照
- `GET_STATS` 和 `GET_STATUS` 都是先在内核里整理好结构体，再 `copy_to_user()`
- 未知命令统一返回 `-ENOTTY`
- `CLR_STATS` 只清统计，不清队列，不改 `seq`，不改 `online`
- `FLUSH_QUEUE` 只清队列，不清统计，不改 `seq`，不改 `online`

### 8.3 用户态怎么调用

`GET_STATS` 的典型调用方式：

```c
struct rf433_stats st = {0};
if (ioctl(fd, RF433_IOC_GET_STATS, &st) == 0) {
	printf("ok=%llu crc=%llu len=%llu drop=%llu\n",
	       st.frame_ok, st.crc_err, st.len_err, st.drop_cnt);
}
```

`GET_STATUS` 的典型调用方式：

```c
struct rf433_status status = {0};
if (ioctl(fd, RF433_IOC_GET_STATUS, &status) == 0) {
	printf("online=%u seq=%u depth=%u cap=%u\n",
	       status.online, status.seq,
	       status.queue_depth, status.queue_capacity);
}
```

`CLR_STATS` 和 `FLUSH_QUEUE` 的典型调用方式：

```c
ioctl(fd, RF433_IOC_CLR_STATS);
ioctl(fd, RF433_IOC_FLUSH_QUEUE);
```

你要抓住的点是：

- 用户态调用时，参数类型必须和头文件里定义的结构体对上
- `ioctl()` 成功返回 0
- `GET_*` 失败一般是用户缓冲区问题，返回 `-EFAULT`

## 9. 在线状态和定时器

先看定时器回调：

```c
static void rf433_online_timer_fn(struct timer_list *t)
{
	struct rf433_priv *priv = from_timer(priv, t, online_timer);
	unsigned long flags;
	unsigned long now = jiffies;

	spin_lock_irqsave(&priv->lock, flags);

	if (time_after(now, priv->last_frame_jiffies + ONLINE_TIMEOUT_SEC * HZ))
		priv->online = false;

	if (priv->state != RF_ST_SYNC0 &&
	    time_after(now, priv->last_byte_jiffies + HALF_FRAME_TIMEOUT))
		parser_reset(priv);

	spin_unlock_irqrestore(&priv->lock, flags);

	mod_timer(&priv->online_timer, jiffies + ONLINE_CHECK_SEC * HZ);
}
```

这段的作用是做两个周期性检查：

1. 超过一定时间没收完整帧，就把 `online` 置为 false
2. 如果卡在半帧状态太久，就把解析器重置

你要抓住的点是：

- `ONLINE_TIMEOUT_SEC = 5`
- `ONLINE_CHECK_SEC = 2`
- `HALF_FRAME_TIMEOUT = HZ`，也就是大约 1 秒

不是“定时器只是打印日志”，而是“定时器在维护驱动健康状态”。

## 10. sysfs 只读属性：辅助观测口

驱动还导出了一组 sysfs 只读属性：

- `frame_ok`
- `crc_err`
- `len_err`
- `drop_cnt`
- `online`
- `seq`

这些属性由 `miscdevice` 对应的 device 节点导出，作用和 ioctl 类似，都是给用户态看状态，但读法更像普通文件读取。

这段的作用是提供一个轻量的观测入口。

你要抓住的点是：

- 这不是主数据通道
- 主数据通道还是 `/dev/rf433` 的 `read()`
- sysfs 更适合快速看单项状态

### 3. 用户态 ABI 一览

这段的作用是把对外接口按“读、等、查、看”拆开。你要抓住的点是：**`read()` 是主数据通道，`poll()` 是就绪探针，`ioctl()` 是控制和快照，sysfs 是只读观测面**。

| ABI | 返回什么 | 阻塞语义 | 可观测字段 / 行为 | 常见错误路径 |
|---|---|---|---|---|
| `read()` | 一整个 `struct rf433_frame`，成功时返回 `sizeof(struct rf433_frame)` | 队列空时阻塞；`O_NONBLOCK` 下不阻塞 | `timestamp_ns / pulse_count / seq / pulse[]` | `count` 太小返回 `-EINVAL`，非阻塞空队列返回 `-EAGAIN`，信号打断返回 `-ERESTARTSYS`，拷贝失败返回 `-EFAULT` |
| `poll()` | 就绪掩码，典型是 `EPOLLIN | EPOLLRDNORM` | 不睡在 `poll()` 自己身上，靠 `rdq` 等通知 | 只告诉你“有没有新帧可读” | 正常路径不拷贝数据，基本没有用户缓冲区类错误 |
| `ioctl()` | 成功返回 `0` | 不阻塞 | `GET_STATS` 看到 `frame_ok/crc_err/len_err/drop_cnt`，`GET_STATUS` 看到 `online/seq/queue_depth/queue_capacity`，`CLR_STATS` / `FLUSH_QUEUE` 改控制面 | 未知命令 `-ENOTTY`，用户缓冲区拷贝失败 `-EFAULT` |
| `sysfs` | 文本快照 | 不阻塞 | `frame_ok / crc_err / len_err / drop_cnt / online / seq` 这些只读状态 | 节点消失后就是 VFS 层的不可访问，不是驱动再返回一帧数据 |

你还要抓住两个边界：
- `read()` 读的是“结构化帧”，不是字节流
- sysfs 读的是“观测值”，不是主数据通道

不是所有接口都在搬同一份数据，而是每个接口承担不同的用户态语义。

### 4. 异常退出与 remove 收尾

这段的作用是把“失败时怎么回滚”和“卸载时怎么收尾”说成一条顺序。你要抓住的点是：**probe 失败时按已初始化资源倒序回滚，remove 时先切断异步源，再拆用户态入口，最后释放底层对象**。

probe 失败路径里，代码已经把回滚顺序写出来了：
- `kfifo_alloc()` 失败时直接返回，这时还没有打开串口，也没有注册设备节点
- `serdev_device_open()` 失败会跳到 `err_fifo`，只需要 `kfifo_free()`
- `misc_register()` 失败会先 `serdev_device_close()`，再 `kfifo_free()`
- `timer_setup()` 和 `mod_timer()` 只在 `misc_register()` 成功之后执行，所以 probe 中途失败时不需要去停一个根本没启动的定时器

remove 的顺序也不是随便写的：
1. `del_timer_sync()` 先停掉定时器，避免回调继续跑
2. `misc_deregister()` 让 `/dev/rf433` 消失，阻止新的 `open()`
3. `serdev_device_close()` 切断 RX 输入面，后续不再进 `receive_buf()`
4. `kfifo_free()` 释放队列内存

你还要抓住“设备节点消失前后”的状态差：
- 在 `misc_deregister()` 之前，用户态还能继续看到 `/dev/rf433`
- 在 `misc_deregister()` 之后，新的 `open()` 进不来，节点也不再暴露
- 已经打开的 fd 不会因为节点消失立刻变成无效对象，但数据源已经被关掉了，所以后续只会看到空队列、等待信号或非阻塞返回

不是“把内存先清了再说”，而是“先让异步生产者全部停下来，再拆入口，再释放对象”。

## 11. 驱动移除时怎么收尾

```c
static void rf433_remove(struct serdev_device *serdev)
{
	struct rf433_priv *priv = serdev_device_get_drvdata(serdev);

	del_timer_sync(&priv->online_timer);
	misc_deregister(&priv->misc);
	serdev_device_close(serdev);
	kfifo_free(&priv->fifo);
}
```

这段的作用是按相反顺序释放资源。

你要抓住的点是：

- 先停定时器，避免回调继续跑
- 再注销 misc 设备，避免用户态继续打开
- 再关闭 serdev
- 最后释放 FIFO

## 12. 按调用链串起来看

如果把整个驱动串成一条链，实际顺序是：

1. 设备树匹配到 `project2,rf433-receiver`
2. `module_serdev_device_driver()` 触发 `rf433_probe()`
3. `probe()` 分配 `rf433_priv`
4. `probe()` 初始化锁、队列、状态机和定时器
5. `probe()` 打开 `serdev` 并注册 `/dev/rf433`
6. 硬件 RX 数据进入 `rf433_receive_buf()`
7. `receive_buf()` 把每个字节喂给 `parser_feed_byte()`
8. `parser_feed_byte()` 完成 `AA55/LEN/PAYLOAD/CRC` 校验
9. 合法帧进入 `parser_emit_frame()`
10. `parser_emit_frame()` 生成 `struct rf433_frame` 并放入 `kfifo`
11. 用户态 `read()` 从 `/dev/rf433` 读出整帧
12. 用户态 `ioctl()` 读取统计或状态，或者清空队列
13. 定时器周期性维护 `online` 和半帧超时

你要抓住的点是：

- 输入链路和输出链路在内核里被接到一起了
- 输入是串口字节流，输出是结构化帧和状态快照

## 13. 用户态应该怎么理解这个驱动

更准确地说，用户态不是在“解析协议”，而是在“消费已经解析好的结果”。

它能拿到的东西只有三类：

- `read()`：一帧 `struct rf433_frame`
- `poll()`：是否有新帧可读
- `ioctl()`：统计、状态、清队列

不是原始串口字节，不是自己去找 `AA55`，也不是自己去算 CRC。  
这些工作都已经在内核里做完了。

## 14. 一句话总结

这份驱动的本质是：

**用 `serdev` 接收硬件字节流，在内核里完成 `AA55/LEN/PAYLOAD/CRC` 解析，把合法帧装进 `kfifo`，再通过 `/dev/rf433`、`ioctl()` 和 sysfs 把结构化结果暴露给用户态。**
