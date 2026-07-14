# STM32 RF 板接入 RK3568 主控

本文面向联调工程师，目标是把 `project2_hardware` 的 USART1 packet 可靠送入 RK3568 serdev 驱动，并明确硬件、驱动与业务层各自增加了什么字段。

## 前置条件与接线

- STM32 固件已按 [硬件教程](../README.md) 烧录，PA9 能看到合法 AA55 packet。
- RK3568 目标内核启用目标 UART、serdev 与 module 支持，设备树可修改。
- STM32 PA9/TX -> RK3568 UART RX；如需回传，RK3568 TX -> PA10/RX；3.3 V 电平、共地。
- 双方都是 9600 8N1、无流控。

不要把 Linux `/dev/tty*` 与项目 `/dev/rf433` 混为一谈：前者是 UART 控制器的一般接口，后者由 `rf433_drv` 绑定 serdev 后导出的定长帧接口。

## 线上的数据

STM32 只发送：

```text
AA 55 | pulse_count(LE16) | pulse_us[](LE16) | XOR checksum
```

不发送 JSON、地址、按键、confidence、时间戳或 seq。驱动校验后构造 `struct rf433_frame`，加入 `timestamp_ns`、`seq`、`reserved`，并原样重建 `pulse_count/pulse[]`。EV1527 地址、key、confidence 是 userland 解码结果，不能倒灌进硬件协议说明。

## 接入步骤

1. 先用逻辑分析仪在 STM32 PA9 验证 packet 长度与 XOR。
2. 在目标 UART 节点下加入 serdev 子节点：

```dts
rf433_receiver {
    compatible = "project2,rf433-receiver";
};
```

实际 UART 节点名、pinctrl 和 `status = "okay"` 由板级 DTS 决定，不能从本仓库猜测。
3. 按 [驱动操作教程](../../project2_master/linux_driver/README.md) 针对同一目标内核构建并加载 `rf433_drv.ko`。
4. 检查：

```bash
dmesg | tail -n 50
ls -l /dev/rf433
cat /sys/class/misc/rf433/online
cat /sys/class/misc/rf433/frame_ok
```

5. 启动 `rf_gateway --rf-input /dev/rf433`，触发遥控并观察 stdout JSON。

## 分层验收

| 层 | 输入 | 成功证据 | 失败时先查 |
|---|---|---|---|
| STM32 | PA0 脉冲 | PA9 合法 AA55/LE16/XOR | 时钟、过滤、FIFO、接线 |
| serdev driver | UART 字节 | `/dev/rf433` 出现、`frame_ok` 增长 | DTS 绑定、波特率、`crc_err/len_err` |
| userland | `rf433_frame` | `rf_event`/`rf_stats` JSON | pulse 数、confidence、稳定策略 |
| MQTT/Qt | JSON payload | topic 或 UI 更新 | broker、envelope、GUI 进程路径 |

legacy `crc_err` 统计实际表示 XOR checksum 不匹配。若它持续增长，不要先调 EV1527 参数；应先修复串口电平、波特率、丢字节或端序。

## 清理与限制

停止网关后再 `sudo rmmod rf433_drv`。主机协议测试只能证明编码/解析契约，不能证明目标内核 DTS、UART pinmux 或电气质量；正式部署必须保留本节的逐层验收记录。
