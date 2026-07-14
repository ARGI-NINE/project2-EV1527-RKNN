# `rf433_drv` v2 设计说明

## 设计目标

把 STM32 USART 的任意分片字节流变为稳定、可 poll、可统计的 `/dev/rf433` 定长帧接口。驱动不做 EV1527 解码、JSON、MQTT 或 Qt 业务。

## 为什么用 serdev + misc device

serdev 让 driver 直接拥有 UART 配置与 receive callback，避免用户态各自重复成帧；misc device 提供简单的 `/dev/rf433`、`read/poll/ioctl/sysfs` ABI。DTS 用 `compatible = "project2,rf433-receiver"` 绑定，UART 固定 9600 8N1 无流控。

## Parser 状态机

状态为 `SYNC0 -> SYNC1 -> LEN0 -> LEN1 -> PAYLOAD -> CRC`。最后名字为 legacy；实际校验是长度与 payload 的逐字节 XOR。设计要求：

- 任意 serdev chunk 边界不影响解析。
- 长度为 0 或超过 1024 立即拒绝并累计 `len_err`。
- 错误 XOR 累计 `crc_err`，不输出部分 frame。
- 在 SYNC1 再遇 0xAA 时保留为新同步开头。
- 半帧超过 1 秒重置，避免永久卡住。

## 输出 ABI

`struct rf433_frame` 包含 wall-clock `timestamp_ns`、`pulse_count`、保留对齐字段、单调 `seq` 和固定 1024 项 pulse 数组。read 的成功单位永远是一整个结构体；这牺牲少量拷贝空间，换取简单稳定的 user ABI。结构布局与 ioctl 编号属于兼容边界。

## 缓冲与并发

合法 frame 进入深度 16 的 kfifo。满时先移除最旧帧、`drop_cnt++`，再加入最新帧；RF 实时监控更重视新鲜度。spinlock 保护 FIFO、统计和 online；wait queue 唤醒阻塞 reader；poll 暴露可读事件。

## 在线语义

任一合法 frame 置 online。timer 每 2 秒检查，连续 5 秒没有合法 frame 置 offline。该状态只说明 RF UART 最近产生了完整校验通过的 frame，不代表 userland 解码或网络成功。

## 可观测性

统计 `frame_ok`、`crc_err`、`len_err`、`drop_cnt`；状态含 online、seq、queue depth/capacity。它们同时通过 ioctl 和 misc-device sysfs 暴露。`RF433_IOC_CLR_STATS` 与 `FLUSH_QUEUE` 是显式运维动作。

## 生命周期不变量

probe 的分配、serdev open、misc register、timer 启动必须有逆序失败清理；remove 先同步删除 timer、注销 misc/关闭 serdev，再释放 kfifo。外部字节、user pointer、queue 并发与 timer 生命周期检查不可为“简化”而删除。

## 非目标与验证

不支持普通 tty/raw file 替代 master 输入，不在 kernel 内解 EV1527，不保证零丢帧。host CTest 只验证同协议实现；driver 发布前还需匹配目标 kernel 构建、DTS 绑定、合法/错误 packet 注入和 read/poll/ioctl/sysfs 实测。具体步骤见 [README.md](README.md)。
