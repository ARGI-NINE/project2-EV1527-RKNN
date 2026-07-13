# rf433_drv README

`rf433_drv` 是 `project2_master` RF 主链里的内核层入口。它的任务非常明确：把硬件侧通过 UART 上送的 RF 脉冲字节流，在内核里还原成稳定的整帧接口 `/dev/rf433`。

## 1. 先记住驱动定位

```text
STM32 UART bytes
  -> serdev client driver
  -> AA55/LEN/PAYLOAD/CRC parser
  -> kfifo of rf433_frame
  -> misc device /dev/rf433
  -> userspace rf_gateway
```

这意味着：

- 它不是串口透传驱动。
- 它不是 EV1527 解码器。
- 它不负责 MQTT，也不负责 Qt。

它负责的是：`byte stream -> frame device`

## 2. 当前代码已经提供的接口

### 2.1 设备节点

- 设备名：`rf433`
- 用户态设备节点：通常是 `/dev/rf433`

### 2.2 帧格式

驱动消费的上行字节流格式是：

```text
AA 55 LEN_LO LEN_HI PAYLOAD[len * 2] CRC
```

其中：

- `LEN` 是脉冲数量，LE16
- `PAYLOAD` 是脉冲宽度数组，元素类型是 `uint16_t`
- `CRC` 是对 `LEN + PAYLOAD` 逐字节 XOR

### 2.2.1 parser 真正按什么规则推进

代码来源：`project2_master/linux_driver/rf433_drv.c::parser_feed_byte()`

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

	default:
		parser_reset(priv);
		break;
	}
}
```

这里的状态机不是抽象描述，而是逐字节硬编码。`SYNC0` 只接受 `0xAA`，`SYNC1` 只接受 `0x55`；如果在 `SYNC1` 又读到一个 `0xAA`，代码不会退回起点，而是留在 `SYNC1`，这就是它容忍连续同步字节的方式。`LEN0/LEN1` 先把脉冲数按小端拼出来，再同步累计 XOR CRC；长度为 `0` 或超出 `RF433_MAX_PULSES` 会立刻 `len_err++` 并复位，不会继续吃后面的 payload。

进入 `PAYLOAD` 后，驱动只做两件事：把每个字节塞进 `payload_buf[]`，同时继续异或进 `crc_accum`。当 `payload_idx == expected_pulses * 2` 时才切到 `CRC`。最后一字节如果与 `crc_accum` 不一致，直接 `crc_err++` 并丢帧；一致时才 `parser_emit_frame()`。最容易忽略的点是第一行 `priv->last_byte_jiffies = jiffies`，它不是为了统计展示，而是给后面的半帧超时恢复逻辑提供时间基准。

### 2.3 用户态读到的结构

用户态不是读到裸字节流，而是读到一个完整 `struct rf433_frame`：

- `timestamp_ns`
- `pulse_count`
- `seq`
- `pulse[]`

代码来源：`project2_master/linux_driver/rf433_ioctl.h::struct rf433_frame`

```c
struct rf433_frame {
    __u64 timestamp_ns;          /* ktime_get_real_ns() at frame-complete */
    __u16 pulse_count;           /* number of valid pulse entries         */
    __u16 reserved;              /* padding, set to 0                     */
    __u32 seq;                   /* monotonic frame sequence number       */
    __u16 pulse[RF433_MAX_PULSES];
};
```

这个 ABI 比共享协议 payload 多了三层信息。`timestamp_ns` 不是发送端时间，而是 Linux 驱动在 `parser_emit_frame()` 成帧时调用 `ktime_get_real_ns()` 打的时间戳；`seq` 是驱动本地单调递增序号，方便用户态判断是否跳号；`reserved` 明确占位成 `0`，保证结构体布局稳定。真正的脉冲数据还是 `pulse_count + pulse[]` 这一层，用户态不需要自己再去恢复 `AA55/LEN/PAYLOAD/CRC`。

这也是为什么 `rf_gateway` 可以直接按帧工作，而不用自己处理串口碎片。

### 2.4 控制面

当前 ioctl 包括：

- `RF433_IOC_GET_STATS`
- `RF433_IOC_CLR_STATS`
- `RF433_IOC_GET_STATUS`
- `RF433_IOC_FLUSH_QUEUE`

当前 sysfs 只读属性包括：

- `frame_ok`
- `crc_err`
- `len_err`
- `drop_cnt`
- `online`
- `seq`

## 3. 与用户态 `rf_gateway` 的关系

`rf_gateway` 是当前代码里 `/dev/rf433` 的唯一现实使用者。它依赖驱动提供三类能力：

1. `read()` 一次得到一整帧脉冲
2. `poll()/epoll` 发现何时有新帧
3. `ioctl()` 读取驱动统计和在线状态

上层所有 `addr`、`key`、`conf`、`rf_event`、`device_status`、`rf_stats` 都不在驱动里生成，而是在用户态 `rf_gateway` 里产生。

### 3.1 `read()` 返回的是定长帧，不是串口流

代码来源：`project2_master/linux_driver/rf433_drv.c::rf433_misc_read()`

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

这段代码把 `/dev/rf433` 的读取语义写得很死。第一道边界就是 `count < sizeof(frame)` 直接 `-EINVAL`，所以用户缓冲区必须至少装下一个完整 `struct rf433_frame`。非阻塞模式只尝试一次 `kfifo_out()`，空队列就返回 `-EAGAIN`；阻塞模式则先睡在 `wait_event_interruptible()` 上，直到 `rdq` 被 `parser_emit_frame()` 唤醒。被信号打断时返回 `-ERESTARTSYS`，醒来后发现队列又空了则返回 `-EIO`，这说明驱动明确把“按帧读取”放在 ABI 第一位，而不是暴露一个随便拼接的字节流口。

成功路径也很固定：`copy_to_user()` 成功后总是返回 `sizeof(frame)`，不是当前脉冲个数，也不是协议字节长度。也正因为如此，用户态主循环可以把 `read()` 的成功返回视为“拿到了一整帧驱动观测结果”。

## 4. `online` 的真实语义

驱动里的 `online` 不是“模块已加载”，也不是“进程已启动”，而是：

- 最近是否持续收到了合法完整帧

因此，正确的系统描述应该是：

- `/dev/rf433` 是驱动导出的帧接口
- `online` 是驱动对 RF 接收活性的判断
- Qt 页面显示在线状态时，实质上是在展示来自驱动和 `rf_gateway` 的观测结果

### 4.1 `online` 与半帧恢复都挂在同一个定时器里

代码来源：`project2_master/linux_driver/rf433_drv.c::rf433_online_timer_fn()`

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

这里真正被定时检查的是两类时间。`last_frame_jiffies` 对应“最近一次成功成帧”的时间，如果超过 `ONLINE_TIMEOUT_SEC` 还没新帧，就把 `online` 拉回 `false`；这就是文档里“在线不是模块加载态，而是最近是否持续收到合法完整帧”的代码依据。另一类是 `last_byte_jiffies`，它不看完整帧，只看 parser 最近一次吃到字节的时刻。

第二个分支专门处理半帧卡死：只要状态机还没回到 `RF_ST_SYNC0`，并且距离上一个字节已经超过 `HALF_FRAME_TIMEOUT`，就直接 `parser_reset(priv)`。这意味着 `online` 和“半帧自恢复”并不是两套平行机制，而是同一个周期定时器一起维护。最后一行 `mod_timer()` 继续把自己挂回 `ONLINE_CHECK_SEC` 之后，所以这不是一次性检查。

## 5. 队列策略

当前实现使用 `kfifo` 做帧队列，满队列时采取：

- 丢弃最旧帧
- 保留最新帧
- `drop_cnt++`

这是一个偏实时性的策略，不是“绝对无丢帧”策略。后续用户态还能根据驱动 `seq` 是否跳号统计应用侧观测到的掉帧。

## 6. 构建

### 本机构建

```bash
cd project2_master/linux_driver
make KDIR=/lib/modules/$(uname -r)/build
```

### 交叉构建

```bash
cd project2_master/linux_driver
make KDIR=/path/to/kernel CROSS_COMPILE=aarch64-linux-gnu- ARCH=arm64
```

## 7. 运行注意事项

- 驱动是 serdev client，需要板级环境里存在与 `compatible = "project2,rf433-receiver"` 匹配的设备。
- 单独拿一个普通主机 `insmod` 并不自动等于“就能看到真实 `/dev/rf433` 数据流”；还需要正确的硬件接线和板级描述。
- 用户态程序默认只接受 `/dev/rf433`，不会把任意文件或任意串口当成等价输入。

## 8. 当前不应写成已实现的内容

这份驱动文档不能越界写成：

- 已支持 RF 指令下发
- 已支持 GPIO 事件输入
- 已支持事件录像联动

从代码事实看，这份驱动只负责 RF 接收侧成帧和基础观测面。

## 9. 继续阅读

如果你想顺着驱动继续往上读，推荐顺序是：

1. `linux_driver/DESIGN_rf433_drv_v2.md`
2. `docs/project2_master_driver_deep_dive.md`
3. `docs/project2_master_userland_deep_dive.md`
