# RF433 Linux 驱动深入说明

本文面向需要修改 `linux_driver/rf433_drv.c` 的内核开发者，解释设计边界与关键不变量。实际构建/加载命令见 [驱动操作教程](../linux_driver/README.md)。

## 数据与线程边界

```text
serdev receive_buf (任意字节分片)
  -> parser_feed_byte
  -> 合法 AA55/LE16/XOR frame
  -> struct rf433_frame
  -> 16-frame kfifo（满时丢最旧）
  -> wait queue / poll
  -> read(/dev/rf433)
```

parser 接受 `AA 55` 同步、LE16 pulse 数、每项 LE16 payload 和一字节 XOR。`crc_err`、`RF_ST_CRC` 是 ABI/实现的 legacy 名字；算法不是多项式 CRC。长度为 0 或超过 `RF433_MAX_PULSES=1024` 立即 `len_err++` 并复位。半帧超过 1 秒会复位，避免 parser 永久挂在 payload 中。

## 队列和 ABI

合法帧获得 `ktime_get_real_ns()` 时间戳与单调 `seq`。kfifo 深度 16，满时跳过最旧帧并增加 `drop_cnt`；这保留实时性。用户态每次成功 read 得到完整 `struct rf433_frame`，缓冲小于结构体返回 `-EINVAL`；非阻塞空队列返回 `-EAGAIN`，阻塞读使用 wait queue。

共享 ABI 位于 `rf433_ioctl.h`：

- `RF433_IOC_GET_STATS`：`frame_ok/crc_err/len_err/drop_cnt`。
- `RF433_IOC_CLR_STATS`：清统计。
- `RF433_IOC_GET_STATUS`：`online/seq/queue_depth/queue_capacity`。
- `RF433_IOC_FLUSH_QUEUE`：清帧队列。

修改结构体布局或 ioctl 编号会影响 userland ABI，不能作为普通内部重构。

## 在线状态与 sysfs

收到合法帧会置 online；定时器每 2 秒检查，5 秒没有合法帧则 offline。sysfs 公开 `frame_ok`、`crc_err`、`len_err`、`drop_cnt`、`online`、`seq`，通常位于 `/sys/class/misc/rf433/`。online 表示最近收到合法 frame，不代表 MQTT 或上层解码成功。

## 必须保留的保护

- serdev 分片与重同步；长度、payload 边界、XOR 和半帧超时。
- spinlock 对 kfifo/统计/online 的保护，wait queue 唤醒和 remove 时 timer 同步。
- read 与查询 ioctl 输出路径的 `copy_to_user`、ioctl magic/size、read 缓冲大小检查；当前 ABI 没有 `copy_from_user` 或写方向 ioctl。
- probe 各资源失败的反向释放与 remove 顺序。

这些不是“多余防御”，而是外部字节流、用户 ABI、并发与资源生命周期边界。可删除的只应是经调用/状态证明永远无效且没有诊断价值的私有冗余。

## 修改后的验证

1. 先运行 user-space 协议 CTest，确认 AA55/LE16/XOR golden vector 与恢复行为。
2. 用匹配目标内核构建 module，无 modpost/ABI 错误。
3. 目标板加载并检查 compatible 绑定、`/dev/rf433`、sysfs。
4. 注入合法、错误 XOR、非法长度、半帧与 burst，观察对应计数和 parser 恢复。
5. 用 poll/read/ioctl 客户端验证完整帧和空队列语义。

主机 CTest 不执行 kernel parser；没有匹配内核和设备时不得声称驱动已运行通过。
