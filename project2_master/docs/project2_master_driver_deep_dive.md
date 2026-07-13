# project2_master RF433 驱动深读

这份文档只读两类文件：

- `project2_master/linux_driver/rf433_drv.c`
- `project2_master/linux_driver/rf433_ioctl.h`

目标不是泛泛地讲“驱动里有什么函数”，而是把 `/dev/rf433` 这条接口怎样从 serdev 长出来读清楚。

兼容说明：以下先补回 `HEAD` 版章节骨架，便于沿用旧目录、旧引用和旧阅读顺序；后文现有正文、源码摘录和细讲全部保留。

## 0. 快速阅读地图

兼容旧版目录：下文现有正文继续覆盖驱动入口、parser、`/dev/rf433` ABI、在线状态和走读顺序，本轮新增代码块与细讲保持不删。

## 1. 先看整体位置

兼容旧版目录：对应下文现有 `## 1. 这份驱动在整条链上的位置`。

## 2. 驱动入口：从模块加载到 probe

兼容旧版目录：对应下文现有 `## 2. 入口：rf433_probe()`。

## 3. 设备节点怎么出来

兼容旧版目录：对应当前正文里 `misc device`、`/dev/rf433`、`probe()` 注册顺序的说明。

## 4. 核心对象：状态、队列、统计、在线状态

兼容旧版目录：对应当前正文里 `struct rf433_priv`、parser 状态、队列和导出 ABI 的讲解。

### 1. 资源生命周期总表

兼容旧版目录：对应当前正文里 `serdev`、`kfifo`、`misc`、`timer` 的创建、回滚与收尾顺序。

### 4.1 `struct rf433_frame`

兼容旧版目录：对应当前正文里用户态帧结构、字段意义和 `read()` 返回契约。

### 4.2 `struct rf433_stats`

兼容旧版目录：对应当前正文里驱动统计字段和 `ioctl` 快照说明。

### 4.3 `struct rf433_status`

兼容旧版目录：对应当前正文里在线位、序号、队列深度与容量说明。

## 5. AA55/LEN/PAYLOAD/CRC 解析状态机

兼容旧版目录：对应当前正文里 parser 状态机、长度校验、CRC 校验和成帧过程。

### 5.1 解析前的复位

兼容旧版目录：对应当前正文里 parser 初始化与异常后回到起点的说明。

### 5.2 按字节喂给状态机

兼容旧版目录：对应当前正文里 `parser_feed_byte()` 的逐字节推进逻辑。

### 5.3 把 payload 变成用户态帧

兼容旧版目录：对应当前正文里 `payload` 转 `struct rf433_frame`、入队与唤醒的说明。

## 6. 硬件侧数据怎么进来

兼容旧版目录：对应下文现有 `## 6. serdev 回调只做一件事：喂 parser`。

### 6.1 串口回调表

兼容旧版目录：对应当前正文里 `serdev_device_ops`、`receive_buf` 和 RX-only 语义。

## 7. file_operations：用户态怎么进来

兼容旧版目录：对应当前正文里 `open/read/poll/ioctl/sysfs` 的导出接口说明。

### 7.1 `open()`

兼容旧版目录：对应当前正文里 `file->private_data` 绑定逻辑。

### 7.2 `read()`

兼容旧版目录：对应当前正文里按帧读取 `struct rf433_frame` 的说明。

#### 非阻塞路径

兼容旧版目录：对应当前正文里 `O_NONBLOCK` 与 `-EAGAIN` 语义。

#### 阻塞路径

兼容旧版目录：对应当前正文里等待队列、`-ERESTARTSYS` 和唤醒后的读取语义。

### 7.3 `poll()`

兼容旧版目录：对应当前正文里 `EPOLLIN | EPOLLRDNORM` 的可读事件说明。

### 2. 并发与唤醒契约

兼容旧版目录：对应当前正文里 `spinlock`、`waitqueue`、`wake_up_interruptible()` 的配合关系。

## 8. ioctl ABI：每个命令到底干什么

兼容旧版目录：对应下文现有 `## 8. /dev/rf433 对用户态导出的 ABI` 中的 `ioctl()` 说明。

### 8.1 ioctl 一览表

兼容旧版目录：对应当前正文里 `GET_STATS`、`CLR_STATS`、`GET_STATUS`、`FLUSH_QUEUE` 的总览。

### 8.2 ioctl 实现

兼容旧版目录：对应当前正文里 `rf433_misc_ioctl()` 的锁保护、快照与返回值说明。

### 8.3 用户态怎么调用

兼容旧版目录：对应当前正文里 `ioctl(fd, ...)` 调用方式和典型字段含义。

## 9. 在线状态和定时器

兼容旧版目录：对应下文现有 `## 7. 在线状态与半帧超时`。

## 10. sysfs 只读属性：辅助观测口

兼容旧版目录：对应下文现有 `### 8.5 sysfs` 及其观测面说明。

### 3. 用户态 ABI 一览

兼容旧版目录：对应当前正文里 `read/poll/ioctl/sysfs` 的整体职责拆分。

### 4. 异常退出与 remove 收尾

兼容旧版目录：对应当前正文里错误回滚、`remove()` 收尾和资源释放顺序说明。

## 11. 驱动移除时怎么收尾

兼容旧版目录：对应当前正文里设备移除、停止定时器、注销节点、关闭 `serdev`、释放队列的顺序。

## 12. 按调用链串起来看

兼容旧版目录：对应当前正文里从 UART 字节流到 `/dev/rf433` 再到用户态消费的主链梳理。

## 13. 用户态应该怎么理解这个驱动

兼容旧版目录：对应当前正文里“驱动只做到 pulse frame、用户态消费已解析结果”的边界说明。

## 14. 一句话总结

兼容旧版目录：对应当前正文末尾的总结合并阅读。

## 1. 这份驱动在整条链上的位置

先把位置立住：

```text
STM32 UART bytes
  -> serdev receive callback
  -> rf433 parser in kernel
  -> kfifo of struct rf433_frame
  -> misc device /dev/rf433
  -> userspace rf_gateway
```

这说明它同时扮演两个角色：

- 向下，它是 serdev client driver，消费 UART 字节流
- 向上，它是 misc device，向用户态提供帧接口

最重要的事实是：用户态看到的不是原始串口字节，而是已经在内核里拼好的 `struct rf433_frame`。

## 2. 入口：`rf433_probe()`

读这份驱动时，最好的起点是 `rf433_probe()`。

它依次做了这些事：

1. 分配 `struct rf433_priv`
2. 初始化锁、等待队列和 parser 状态
3. 分配帧队列 `kfifo`
4. 把 `rf433_serdev_ops` 绑到 serdev 设备
5. 打开 serdev，并设置 9600 波特率、无流控、无校验
6. 注册 misc 设备，名字是 `rf433`
7. 启动在线状态定时器

因为 `misc.name = "rf433"`，所以用户态最终看到的设备节点通常就是 `/dev/rf433`。

来源：`project2_master/linux_driver/rf433_drv.c`，函数：`rf433_probe()`，作用：把 parser、`kfifo`、serdev 和 `misc` 设备一次性挂起来，形成 `/dev/rf433`。

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

	timer_setup(&priv->online_timer, rf433_online_timer_fn, 0);
	mod_timer(&priv->online_timer, jiffies + ONLINE_CHECK_SEC * HZ);

	dev_info(dev, "rf433 serdev driver probed (%d baud)\n", BAUD_RATE);
	return 0;
}
```

这段实现把文档里的 7 个步骤压成了可执行事实：

- `parser_reset(priv)` 说明 parser 初始态就是 probe 时建立的，不等第一帧来了再懒初始化。
- `kfifo_alloc()` 和 `misc_register()` 说明 `/dev/rf433` 不是单纯的 serdev 回调，而是额外长出来的一层内核队列 ABI。
- `priv->misc.groups = rf433_groups` 也解释了为什么这个驱动同时有字符设备接口和 sysfs 观测面。

## 3. 私有状态：`struct rf433_priv`

这份结构体是整条驱动的核心上下文。阅读时建议按职责拆开看：

来源：`project2_master/linux_driver/rf433_drv.c`，结构：`struct rf433_priv`，作用：集中保存 serdev 句柄、parser 状态、输出队列、统计和在线检测。

```c
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
```

这份结构体本身已经把驱动分层写得很直白：

- `state/expected_pulses/payload_idx/payload_buf/crc_accum` 是纯 parser 状态，不掺用户态语义。
- `fifo/rdq` 才是面向 `/dev/rf433` 的交付层。
- `stats/seq/online_timer/last_frame_jiffies/last_byte_jiffies/online` 则是 Linux 驱动自己追加的运行时观测语义。

### 3.1 serdev 资源

- `struct serdev_device *serdev`

这是向下连接 UART 的抓手。

### 3.2 parser 状态

- `state`
- `expected_pulses`
- `payload_idx`
- `payload_buf[]`
- `crc_accum`

这一组字段负责把串口字节流拼成一帧。

### 3.3 输出队列

- `DECLARE_KFIFO_PTR(fifo, struct rf433_frame)`
- `wait_queue_head_t rdq`

这是驱动向用户态交付整帧的核心桥梁。

### 3.4 统计与在线状态

- `struct rf433_stats stats`
- `u32 seq`
- `last_frame_jiffies`
- `last_byte_jiffies`
- `bool online`

这些字段分别服务于：

- 统计错误和掉帧
- 生成单调序号
- 判断是否长期无新帧
- 避免 parser 卡死在半帧状态

## 4. parser 是怎样把字节流变成帧的

### 4.1 `parser_reset()`

这个函数把状态机恢复到起点：

- 回到 `RF_ST_SYNC0`
- 清空期望脉冲数
- 清空 payload 进度
- 清空 CRC 累加器

任何长度错误、CRC 错误、超时半帧，最终都会走回这里。

来源：`project2_master/linux_driver/rf433_drv.c`，函数：`parser_reset()`，作用：把 AA55 parser 拉回起始态。

```c
static void parser_reset(struct rf433_priv *priv)
{
	priv->state           = RF_ST_SYNC0;
	priv->expected_pulses = 0;
	priv->payload_idx     = 0;
	priv->crc_accum       = 0;
}
```

这里没有做任何“保留部分上下文继续猜”的动作，说明当前驱动选择的是严格丢弃坏半帧，而不是容错拼接。

### 4.2 `parser_feed_byte()`

这是驱动里真正的逐字节状态机。它走的状态序列与共享协议文档一致：

```text
RF_ST_SYNC0 -> RF_ST_SYNC1 -> RF_ST_LEN0 -> RF_ST_LEN1 -> RF_ST_PAYLOAD -> RF_ST_CRC
```

逐段理解：

- `SYNC0/SYNC1`
  只接受 `0xAA 0x55` 帧头。
- `LEN0/LEN1`
  组装小端脉冲数，并开始累计 CRC。
- `PAYLOAD`
  持续收 `pulse_count * 2` 个字节。
- `CRC`
  比较接收到的 CRC 与累加值，成功则出帧，失败则记错并复位。

这条状态机做的事情很克制：只负责成帧，不负责 EV1527 解码。

来源：`project2_master/linux_driver/rf433_drv.c`，函数：`parser_feed_byte()`，作用：逐字节消费 UART 数据，按共享协议状态机拼出一帧。

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
	}
}
```

逐段对照代码看，有几个细节很关键：

- `last_byte_jiffies = jiffies` 不是装饰字段，后面的半帧超时逻辑直接依赖它。
- `LEN1` 阶段一旦发现 `0` 或超上限，马上记 `len_err++` 并 reset，说明长度错误根本不会进入 payload 阶段。
- `CRC` 成功路径不是直接把原始字节吐给用户态，而是立即调用 `parser_emit_frame()`，所以成帧责任完整停留在内核里。

### 4.3 `parser_emit_frame()`

一旦 CRC 校验通过，驱动会：

1. 新建一个 `struct rf433_frame`
2. 写入 `timestamp_ns = ktime_get_real_ns()`
3. 写入 `pulse_count`
4. 递增并写入 `seq`
5. 把 payload 字节两两还原成 `pulse[]`
6. 推入 `kfifo`
7. 更新 `frame_ok`
8. 唤醒等待中的读者

这里的关键补充是：`timestamp_ns` 和 `seq` 都是驱动层附加出来的本地元数据，不是共享脉冲协议的一部分。

来源：`project2_master/linux_driver/rf433_drv.c`，函数：`parser_emit_frame()`，作用：把 parser 暂存的 payload 还原成 `struct rf433_frame`，并推入 `kfifo`。

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
```

这段代码把“共享 pulse frame”和“驱动本地扩展”切得很清楚：

- `frame.pulse[]` 来自 payload 两字节一组的小端还原，这部分仍然是共享协议语义。
- `timestamp_ns`、`seq`、`online`、`drop_cnt` 都是 Linux 驱动本地运行时语义。
- `kfifo_is_full()` 时先 `kfifo_skip()` 再 `kfifo_in()`，明确说明它追求最新帧优先，而不是历史帧完整保留。

## 5. serdev 回调只做一件事：喂 parser

`rf433_receive_buf()` 的逻辑非常纯：

1. 取出 `rf433_priv`
2. 加锁
3. 按字节遍历输入缓冲
4. 每个字节调用 `parser_feed_byte()`
5. 解锁
6. 返回已消费字节数

这说明驱动的成帧逻辑完全在内核里完成，不会把碎片字节推给用户态补全。

来源：`project2_master/linux_driver/rf433_drv.c`，函数：`rf433_receive_buf()`，作用：作为 serdev RX 回调，把一批串口字节逐个喂给 parser。

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

这里没有任何中间缓存、工作队列或底半部搬运逻辑；驱动对 RX 的核心判断就是“持锁后立即逐字节推进状态机”。

## 6. 队列策略：满了就丢最旧帧

`parser_emit_frame()` 里有一个非常值得记住的策略：

- 如果 `kfifo` 已满，先 `kfifo_skip()` 丢弃最旧的一帧
- 然后把最新帧入队
- 同时 `drop_cnt++`

这是一个明显偏实时性的策略。它说明当前驱动更在意“让用户态尽量看到最新 RF 数据”，而不是“绝不丢任何历史帧”。

后面的 `rf_gateway` 也会基于 `seq` 统计驱动侧是否出现跳号，从而形成用户态的 `drv_drop` 统计。

## 7. 在线状态与半帧超时

`rf433_online_timer_fn()` 每隔固定时间检查两件事：

来源：`project2_master/linux_driver/rf433_drv.c`，函数：`rf433_online_timer_fn()`，作用：周期性刷新在线状态，并清理卡死在中间态的半帧。

```c
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
```

这也解释了为什么驱动的 `online` 语义是“最近收到过完整合法帧”，而不是“serdev 已经打开”。

### 7.1 有没有长期没有完整新帧

如果超过 `ONLINE_TIMEOUT_SEC` 没有完整新帧，就把 `online = false`。

这意味着驱动的在线语义是：

“最近是否持续收到了合法完整帧”

而不是：

“驱动模块是否已经加载”或“进程是否还活着”

### 7.2 parser 是否卡在半帧

如果状态机不在起始态，且距离 `last_byte_jiffies` 已超过 `HALF_FRAME_TIMEOUT`，就强制 `parser_reset()`。

这样可以避免因为串口中途断流而让 parser 永远挂在某个中间状态。

## 8. `/dev/rf433` 对用户态导出的 ABI

### 8.1 `struct rf433_frame`

`rf433_ioctl.h` 里定义了用户态读到的整帧结构：

- `timestamp_ns`
- `pulse_count`
- `reserved`
- `seq`
- `pulse[RF433_MAX_PULSES]`

其中只有 `pulse_count` 和 `pulse[]` 属于“脉冲帧本体”，其余字段都是 Linux 侧为了观测性追加的元数据。

来源：`project2_master/linux_driver/rf433_ioctl.h`，结构：`struct rf433_frame`，作用：定义 `read()` 每次交付给用户态的一整帧 ABI。

```c
struct rf433_frame {
    __u64 timestamp_ns;          /* ktime_get_real_ns() at frame-complete */
    __u16 pulse_count;           /* number of valid pulse entries         */
    __u16 reserved;              /* padding, set to 0                     */
    __u32 seq;                   /* monotonic frame sequence number       */
    __u16 pulse[RF433_MAX_PULSES];
};
```

这一定义直接证明了两件事：

- `read()` 看到的不是可变长串口字节，而是固定外壳的 struct。
- 共享协议里的 `rf_frame_t` 只关心 `pulse_count/pulse[]` 等价物，`timestamp_ns/seq` 是 Linux 驱动追加的 ABI 扩展。

### 8.2 `read()`

`rf433_misc_read()` 的语义是：

- 调用者缓冲区必须至少容纳一个完整 `struct rf433_frame`
- 非阻塞模式下，队列空则返回 `-EAGAIN`
- 阻塞模式下，会睡眠等待直到队列非空
- 成功时一次返回一整帧，而不是可变长字节片段

所以用户态读 `/dev/rf433` 的正确理解是：

“每次读一个定长外壳，里面用 `pulse_count` 标出有效脉冲数”

来源：`project2_master/linux_driver/rf433_drv.c`，函数：`rf433_misc_read()`，作用：从 `kfifo` 取一帧并复制到用户缓冲区。

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

这里最关键的不是 `copy_to_user()`，而是 `return sizeof(frame)`。这行把 ABI 语义钉死成了“一次一整帧”。

### 8.3 `poll()`

`rf433_misc_poll()` 只有在队列非空时才返回 `EPOLLIN | EPOLLRDNORM`。这正是 `linux_app/rf_epoll.c` 能直接把 `/dev/rf433` 纳入 epoll 事件循环的原因。

来源：`project2_master/linux_driver/rf433_drv.c`，函数：`rf433_misc_poll()`，作用：把 `kfifo` 非空状态映射成标准可读事件。

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

`poll_wait()` 绑定的是 `rdq`，而唤醒点在 `parser_emit_frame()` 的 `wake_up_interruptible(&priv->rdq)`。所以 read/poll 是一套闭环，不是两条独立逻辑。

### 8.4 `ioctl()`

当前导出的控制面有四个：

| ioctl | 作用 |
| --- | --- |
| `RF433_IOC_GET_STATS` | 读取 `frame_ok/crc_err/len_err/drop_cnt` |
| `RF433_IOC_CLR_STATS` | 清空统计 |
| `RF433_IOC_GET_STATUS` | 读取 `online/seq/queue_depth/queue_capacity` |
| `RF433_IOC_FLUSH_QUEUE` | 清空帧队列 |

这也是 `rf_gateway` 能定期组装 `device_status` 和 `rf_stats` 的基础。

来源：`project2_master/linux_driver/rf433_drv.c`，函数：`rf433_misc_ioctl()`，作用：导出统计、状态和队列控制面。

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

这里的 `GET_STATUS` 和 `GET_STATS` 是用户态观测面的主入口；Qt 看到的很多状态字段并不是 read 帧自带，而是 `rf_gateway` 额外通过这些 ioctl 拉出来的。

### 8.5 sysfs

驱动还导出了只读 sysfs 属性：

- `frame_ok`
- `crc_err`
- `len_err`
- `drop_cnt`
- `online`
- `seq`

它们的作用不是替代 `/dev/rf433`，而是提供运行时观察面。

来源：`project2_master/linux_driver/rf433_drv.c`，宏与函数：`RF433_SYSFS_RO` / `online_show()`，作用：把统计字段和 `online` 映射成只读 sysfs 属性。

```c
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
```

这套 sysfs 明确只暴露“当前值”，不暴露帧数据本体。所以它适合做健康检查和调试，不适合代替 `/dev/rf433` 消费业务帧。

## 9. 为什么这份驱动只做到 pulse frame

就当前代码分层而言，这是一个刻意的边界：

- 驱动负责“字节流 -> 整帧脉冲”
- 用户态负责“整帧脉冲 -> EV1527 解码结果 -> JSON/MQTT”

这样做的好处是：

1. 内核不需要承担高层协议语义和业务策略。
2. `rf_gateway` 可以自由调整稳定分组、去重和 JSON 输出，而不用碰内核。
3. Qt 页面也只依赖稳定的用户态 JSON 契约，而不是直接依赖内核细节。

## 10. 驱动与 `rf_gateway` 的直接关系

把这一段单独拎出来，是因为它决定了后续文档怎么写。

`rf_gateway` 从驱动取走的是：

- `pulse_count`
- `pulse[]`
- `timestamp_ns`
- `seq`

然后它自己负责：

- 调用 EV1527 解码器
- 生成 `addr`、`key`、`confidence`
- 统计应用侧跳帧
- 组装 `device_status/rf_stats/rf_event`
- 输出 `stdout JSON envelope`
- 旁路 MQTT publish

所以 `/dev/rf433` 是 `rf_gateway` 的输入，不是 Qt 的输入。

## 11. 推荐的走读顺序

如果你打算对着代码读，建议按这个顺序：

1. `rf433_probe()`
   先看资源怎样挂起来。
2. `struct rf433_priv`
   再看上下文里都维护了哪些状态。
3. `parser_reset()` / `parser_feed_byte()` / `parser_emit_frame()`
   看“字节流如何变整帧”。
4. `rf433_receive_buf()`
   看 serdev 如何把输入喂给 parser。
5. `rf433_misc_read()` / `rf433_misc_poll()` / `rf433_misc_ioctl()`
   看 `/dev/rf433` 的用户态契约。
6. `rf433_online_timer_fn()`
   最后补齐在线状态和半帧超时的运行时语义。
