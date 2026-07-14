# `rf433_drv` 构建、加载与验收教程

## 目标与前置条件

本文面向 RK3568 板端部署者。完成后应看到 serdev 成功绑定、`/dev/rf433` 可读、ioctl/sysfs 统计正确，并能安全卸载。

需要：与运行板内核完全匹配的源码/配置/Module.symvers、交叉编译器（或板上 native 编译）、有 serdev 的目标 UART、STM32 9600 8N1 输入、root 权限。普通主机只编译成功不能证明板端 ABI/绑定成功。

## 设备树

在实际 UART controller 节点下添加：

```dts
rf433_receiver {
    compatible = "project2,rf433-receiver";
};
```

同时设置 controller 的 pinctrl 和 `status = "okay"`。具体 `uartX` 名称由板级 DTS 决定；不要照抄一个不存在的节点。serdev 会接管该 UART，因此不要再由另一个 tty client 同时占用。

## 构建

在目标板 native build：

```bash
cd project2_master/linux_driver
make KDIR=/lib/modules/$(uname -r)/build
```

交叉构建：

```bash
make KDIR=/path/to/matching/kernel \
  ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu-
```

成功应生成 `rf433_drv.ko` 且 modpost 无 unresolved symbol/version 错误。若目标内核版本、config 或 Module.symvers 不匹配，先修复内核环境，不要强制加载。

## 加载与权限

```bash
sudo insmod rf433_drv.ko
dmesg | tail -n 50
ls -l /dev/rf433
```

成功日志包含 `rf433 serdev driver probed (9600 baud)`，并出现 misc device。若无 `/dev/rf433`：检查 DTS compatible、UART status/pinctrl、module 是否加载、是否被其它驱动占用。

临时测试可由 root 读取；长期部署应写精确 udev 规则或服务用户组，不建议长期 `chmod 666`。

## 观察 sysfs

```bash
for f in frame_ok crc_err len_err drop_cnt online seq; do
  printf '%s=' "$f"
  cat "/sys/class/misc/rf433/$f"
done
```

- `frame_ok`：进入 FIFO 的合法帧数。
- `crc_err`：legacy 名称，实际为 XOR 不匹配。
- `len_err`：0 或超过 1024 的 pulse count。
- `drop_cnt`：16-frame FIFO 满时丢弃旧帧。
- `online`：最近 5 秒收到合法帧；检查周期 2 秒。

## 读、poll 与 ioctl

`read()` 不是裸 UART；每次成功返回完整 `struct rf433_frame`，定义在 `rf433_ioctl.h`。缓冲必须至少为该结构大小。`rf_gateway` 已按该 ABI 实现。若按本仓库其它教程从仓库根构建到 `build/master`，从根目录运行：

```bash
./build/master/linux_app/rf_gateway --rf-input /dev/rf433
```

自写工具应包含同一 `rf433_ioctl.h`，并可使用 `RF433_IOC_GET_STATS`、`CLR_STATS`、`GET_STATUS`、`FLUSH_QUEUE`。不要用 `cat` 输出判断结构字段，因为二进制含大量固定数组空间；用结构化 reader 或 gateway。

## 注入/联调成功标准

1. 合法 AA55/LE16/XOR packet 使 `frame_ok`、`seq` 增长，poll 唤醒，read 返回完整结构。
2. 改坏末字节只增加 `crc_err`，下一合法 packet 仍能恢复。
3. 非法 length 增加 `len_err`。
4. 连续 burst 超过消费能力时 `drop_cnt` 可增长，但 driver 保留最新帧。
5. 停止输入约 5 秒后 online 变 0；恢复合法输入后变 1。

## 安全清理

```bash
# 先停止 rf_gateway/Qt 和其它 reader
sudo rmmod rf433_drv
dmesg | tail -n 30
make KDIR=/path/to/matching/kernel clean
```

`rmmod` 提示 busy 时，先查仍打开 `/dev/rf433` 的进程，不要强制卸载。详细设计不变量见 [DESIGN_rf433_drv_v2.md](DESIGN_rf433_drv_v2.md)，userland 见 [../docs/project2_master_userland_deep_dive.md](../docs/project2_master_userland_deep_dive.md)。
