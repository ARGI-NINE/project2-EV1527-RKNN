# AA55 共享脉宽协议

本文是 UART wire format 的唯一语义说明，适用于 STM32、Linux driver、master、PC simulator 和独立 `project2_test`。

## 格式

```text
AA 55 | pulse_count:LE16 | pulse[0]:LE16 ... pulse[n-1]:LE16 | checksum:U8
```

- 同步固定 `0xAA 0x55`。
- `pulse_count` 范围 1..1024。
- 每项是微秒脉宽，16-bit little-endian。
- checksum 初值 0，对 `pulse_count` 两字节和整个 payload 逐字节 XOR；不包含 AA55，也不包含 checksum 自身。

代码中的 `rf_proto_crc8`、driver 的 `RF_ST_CRC`/`crc_err` 是 legacy 标识符。这里没有 CRC 多项式、反射或初值参数，不能称为密码学完整性。

## Golden vector

frame `{len=1, pulse[0]=0x1234}`：

```text
AA 55 01 00 34 12 27
```

`0x01 ^ 0x00 ^ 0x34 ^ 0x12 = 0x27`。

## 实现分布与构建成员

| 位置 | encoder | parser | 用途 |
|---|---:|---:|---|
| `project2_hardware/Hardware/RF_Protocol.c` | 是 | 是 | 固件发送，parser 可复用 |
| `project2_master/common/rf_protocol.c` | 是 | 否 | 已编入 `rf_gateway_core` 与 test |
| `project2_master/linux_driver/rf433_drv.c` | 否 | 是 | 内核 serdev 字节流到 ABI frame |
| `project2_pc_sim/common/rf_protocol.c` | 是 | 是 | stdin replay/parser tests |
| `../project2_test/hardware/Hardware/RF_Protocol.c` | 是 | 是 | 独立仓库自己的副本 |

独立副本避免两个 Git 仓库构建耦合，但协议修改必须同步更新全部实现与契约测试。

## Parser 恢复

parser 应处理任意分片：搜索 AA，再等 55；SYNC1 再遇 AA 可继续等待 55；无效长度或错误 XOR 丢弃当前候选并回到同步。kernel parser 还有 1 秒半帧 timeout。恢复检查不能删，因为串口、stdin 和测试都属于外部输入边界。

## 与 EV1527 的区别

AA55 是 STM32 到主控的“脉宽数组传输协议”；EV1527 是这些 pulse 表示的无线码型。AA55 frame 可以协议合法但 EV1527 解码失败。当前 decoder 通常使用同步 pulse 加 24 bit 高/低对，完整候选约 50 pulse。

## 测试

```bash
cmake -S project2_master -B build/master-proto \
  -DBUILD_LINUX_APP=OFF -DBUILD_QT5_GUI=OFF -DBUILD_TESTING=ON
cmake --build build/master-proto
ctest --test-dir build/master-proto --output-on-failure

cmake -S project2_pc_sim -B build/pc-proto \
  -DBUILD_LINUX_APP=ON -DBUILD_QT5_GUI=OFF -DBUILD_TESTING=ON
cmake --build build/pc-proto
ctest --test-dir build/pc-proto --output-on-failure
```

再在 `../project2_test` 独立运行其 CTest。主机测试覆盖 golden encode、bad-XOR recovery、oversized length 和 decode 长度边界；不覆盖真实 UART 电气或 kernel parser 执行。
