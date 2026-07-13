# rf433_drv v2 设计说明

这份设计文档只讨论当前 `rf433_drv` 的内核层设计目标和取舍，不讨论 PC 仿真链路，也不把上层 `rf_gateway` 的逻辑混进来。

## 1. 设计目标

当前驱动要解决的核心问题只有一个：

把硬件侧 UART 上传的 RF 脉冲字节流，在内核里还原成一个稳定、可 epoll、可观测的用户态帧接口 `/dev/rf433`。

展开成工程目标，就是：

1. 在内核里完成 `AA55/LEN/PAYLOAD/CRC` 成帧
2. 对用户态导出定长结构体 `struct rf433_frame`
3. 让用户态不再处理串口碎片和半帧问题
4. 提供最基本的统计与在线状态观察面

## 2. 分层原则

这份设计非常强调分层边界。

### 驱动负责什么

- 接收 UART 字节流
- 解析共享脉冲帧格式
- 维护帧队列
- 导出 `read/poll/ioctl/sysfs`
- 维护 `online`、`seq` 和错误统计

### 驱动不负责什么

- EV1527 解码
- `addr/key/conf` 生成
- JSON envelope
- MQTT publish
- Qt 页面展示

因此，设计边界必须停在 pulse frame，而不是越界到业务语义。

## 3. 为什么选 serdev + misc device

### 3.1 serdev

向下用 serdev，是因为当前硬件接入方式本质上是一个串口从设备：

- 底层接收单位是字节
- 上层协议自己定义帧头、长度和 CRC

serdev 非常适合这种“串口字节流 + 上层私有协议”的模型。

### 3.2 misc device

向上用 misc device，是因为用户态真正需要的是一个简单稳定的帧接口：

- 能 `open()`
- 能 `read()`
- 能 `poll()/epoll`
- 能 `ioctl()`

对于当前项目而言，`/dev/rf433` 比“继续把原始串口暴露给用户态再自己成帧”更符合真实需求。

## 4. 协议状态机设计

### 4.1 状态定义

当前 parser 状态机为：

```text
SYNC0 -> SYNC1 -> LEN0 -> LEN1 -> PAYLOAD -> CRC
```

设计意图很清楚：

- `SYNC0/SYNC1`
  先完成帧头同步。
- `LEN0/LEN1`
  提前知道本帧需要多少 payload 字节。
- `PAYLOAD`
  连续接收脉冲宽度数组。
- `CRC`
  在最终出帧前做完整性验证。

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

设计文档里这条状态链不是伪代码，而是上面这段分支结构。`RF_ST_SYNC1` 对连续 `0xAA` 采取“留在同步第二阶段”的策略，这让 parser 能跨过重复同步字节继续找 `0x55`。`LEN0/LEN1` 不只是读长度，还同步累计 `crc_accum`，这样后面 `PAYLOAD` 段只需继续 XOR 新字节即可。非法长度在 `LEN1` 就被切断，原因也很明确：一旦 `expected_pulses` 为 `0` 或超出 `RF433_MAX_PULSES`，后续 payload 长度已经不可相信，再继续推进只会把 parser 带进坏状态。

最后的 `CRC` 分支也体现了设计目标：坏帧不做“部分恢复”，而是 `crc_err++` 后立刻 `parser_reset(priv)`；好帧则先 `parser_emit_frame(priv)` 再复位。再往前看第一行 `priv->last_byte_jiffies = jiffies`，就能看出这份设计和 `online` / 半帧超时是耦合的，parser 每吃一个字节都会刷新“最近活动时间”。

### 4.2 错误处理

对当前协议而言，最关键的错误只有两类：

- 长度非法
- CRC 不匹配

两者都会立即复位状态机，并累加对应统计。这样设计的核心目的是：

- 尽快摆脱坏帧
- 尽快回到下一次同步机会

## 5. 半帧超时设计

如果串口在一帧中途断了，而状态机又一直停在中间态，就会污染后续接收。

因此当前实现增加了“半帧超时复位”：

- 如果状态机不在起始态
- 且距离最近一个字节已经超过 `HALF_FRAME_TIMEOUT`
- 就强制回到 `SYNC0`

这条设计确保 parser 具有自恢复能力，而不是依赖用户态重启。

## 6. `online` 设计

`online` 的设计目标不是表达“驱动活着”，而是表达：

- 最近是否持续接收到合法完整帧

当前策略是：

- 每次完整合法帧入队时置 `online = true`
- 如果一段时间没有新完整帧，则定时器把它置回 `false`

这样用户态看到的在线位更接近“RF 接收链路是否真的在出有效数据”，而不是“软件进程是否存在”。

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

这段实现说明 `online` 设计里其实有两套判据。第一套盯 `last_frame_jiffies`，只看“最近多久没成功成帧”，超过 `ONLINE_TIMEOUT_SEC` 就判离线；第二套盯 `last_byte_jiffies` 和 `priv->state`，只要 parser 卡在中间态并且超过 `HALF_FRAME_TIMEOUT` 没新字节，就强制 `parser_reset()`。这就是为什么设计文档里把“在线状态”与“半帧自恢复”放在同一节讲，它们在实现里本来就是由同一个 timer callback 管的。

最后的 `mod_timer()` 也很重要，它把定时器周期性挂回 `ONLINE_CHECK_SEC` 之后，说明这里不是 probe 时跑一次的初始化检查，而是驱动生命周期内持续维护的后台机制。

## 7. 队列设计与掉帧策略

### 7.1 为什么需要队列

serdev 回调是中断/驱动侧上下文，用户态读则是另外一条节奏。两者之间必须有缓冲层，所以用了 `kfifo`。

### 7.2 为什么满了丢最旧帧

当前队列策略是：

- 队列满时先丢最旧帧
- 再保留最新帧

这是一个偏实时性的取舍：

- 更适合“我要尽快看到现在发生了什么”
- 不适合“我要绝对保留全部历史帧”

这和当前项目的 RF 事件监测定位是相符的。

## 8. 用户态 ABI 设计

### 8.1 `read()`

设计目标是“一次读一个完整帧”，而不是“给用户态继续拼字节”。

所以 `read()` 返回的是完整 `struct rf433_frame`，其中：

- `pulse_count` 标记有效脉冲数
- `pulse[]` 提供脉冲宽度数组
- `timestamp_ns` / `seq` 提供本地观测元数据

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

这个结构体就是用户态 ABI，不是示意图。`timestamp_ns` 和 `seq` 明确把驱动本地观测信息带到用户态，使上层可以同时做时序分析和跳号检测；`reserved` 则把对齐和未来扩展空间固定进 ABI。真正跨内核/用户态传输的核心数据仍然只有 `pulse_count + pulse[]`，但外层打包后的 `struct rf433_frame` 才是 `/dev/rf433` 的读取单位。

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

这里的实现把“按帧读”这个设计目标彻底落死了。首先，调用方必须给出至少 `sizeof(struct rf433_frame)` 的缓冲区，否则直接 `-EINVAL`；其次，非阻塞读在队列空时返回 `-EAGAIN`，阻塞读睡在 `wait_event_interruptible()` 上，被信号打断则返回 `-ERESTARTSYS`。也就是说，用户态完全不需要自己攒串口碎片或估长度，成功一次就是拿到一整帧结构体。

`ret == 0` 时阻塞分支返回 `-EIO` 也值得注意，这代表驱动作者把“被唤醒但队列已空”视为异常竞争路径，而不是悄悄返回 0 字节。最后固定返回 `sizeof(frame)`，进一步说明它不是“读了多少就给多少”的流接口，而是 record-oriented ABI。

### 8.2 `poll()`

设计目标是让用户态能很自然地接入 `epoll` 主循环。`rf_gateway` 正是按这个思路实现的。

### 8.3 `ioctl()`

设计目标是把统计和在线状态从数据面剥离出来，避免用户态靠猜测推断驱动状态。

当前导出的 `GET_STATS` 和 `GET_STATUS` 正服务于这个目标。

## 9. 为什么不在驱动里做 EV1527 解码

这是最重要的非目标之一。

当前不把 EV1527 解码塞进内核，原因很明确：

1. 解码策略和阈值仍可能调整。
2. 稳定分组、近邻合并、重复抑制都属于业务策略，更适合用户态。
3. JSON/MQTT/Qt 这些上层输出天然属于用户态。

因此设计上故意保持：

`driver = pulse frame`

而不是：

`driver = full business event`

## 10. 与 `rf_gateway`、MQTT、Qt 的接口边界

当前系统边界可以明确写成：

```text
driver output: /dev/rf433
userland output: stdout JSON envelope
network side-branch: MQTT publish
UI consumer: Qt RF page
```

也就是说：

- 驱动不认识 MQTT
- 驱动不认识 Qt
- 驱动更不认识 `addr/key/conf`

这些都发生在用户态之后。

## 11. 当前未纳入设计目标的内容

这份设计文档不应把下面内容写成已完成设计：

- MQTT command 下发
- GPIO 事件接入
- 事件录像联动闭环

从现有代码事实看，它们都不属于当前驱动设计的落地范围。

## 12. 如何使用这份设计文档

推荐配合源码按下面顺序阅读：

1. 先看 `struct rf433_priv`
2. 再看 `parser_feed_byte()` 和 `parser_emit_frame()`
3. 再看 `rf433_misc_read()/poll()/ioctl()`
4. 最后看 `rf433_online_timer_fn()`

如果读完之后你仍然能坚持下面这句话，说明驱动边界已经读对了：

“这份驱动只把 UART 字节流变成 `/dev/rf433` 脉冲帧接口；解码、JSON、MQTT、Qt 都在它上面。”
