# STM32 固件分册 3：协议、Timer 与辅助发送

## 串口协议

当前 wire format：

```text
offset  size                 field
0       1                    0xAA
1       1                    0x55
2       2                    pulse_count, little-endian
4       pulse_count * 2      pulse_us[], each little-endian
...     1                    XOR checksum
```

checksum 初值为 0，从 offset 2 的长度低字节一直异或到 payload 末字节。源码函数因历史兼容叫 `rf_proto_crc8`，解析状态/统计也可能带 `CRC`/`crc_err`，但算法是 XOR，不是多项式 CRC。

Golden vector：`pulse_count=1`、`pulse[0]=0x1234` 时，packet 为：

```text
AA 55 01 00 34 12 27
```

因为 `01 xor 00 xor 34 xor 12 = 27`。`rf_proto_encode` 对空 frame、0 pulse、超过 1024 pulse 或输出缓冲不足返回 0；parser 会拒绝无效长度/错误 XOR，并搜索下一组同步字节恢复。

## 三份本地实现

- 固件：`project2_hardware/Hardware/RF_Protocol.c`
- master encoder：`project2_master/common/rf_protocol.c`（已链接进 `rf_gateway_core`）
- PC simulator：`project2_pc_sim/common/rf_protocol.c`（另含 parser）

独立仓库 `project2_test` 有自己的协议副本，避免构建跨仓库引用。改变 wire format 时必须同步检查这些副本及三处 CTest，不能只改一处文档。

## Timer

`system/Timer.c` 使用 TIM3 提供微秒时间与 1 ms tick；`TIM3_IRQHandler` 驱动 `RF_Capture_Tick1msHandler`，使 idle frame 能在没有后续边沿时完成。`Timer_NowUs` 可供辅助代码读取。板级时钟改变后，应以示波器/已知间隔复核，不要只依据编译通过。

## RF_Tx 与 Delay

`Hardware/RF_Tx.*` 提供 GPIO 脉冲重放，`system/Delay.*` 提供忙等延时。它们是公开辅助模块，当前生产 `User/main.c` 没有调用。保留这些 API 是支持范围选择，并不意味着已进入接收主链路或已做实时精度验证。

## 主机验证

```bash
cmake -S project2_master -B build/master-tests \
  -DBUILD_LINUX_APP=OFF -DBUILD_QT5_GUI=OFF -DBUILD_TESTING=ON
cmake --build build/master-tests
ctest --test-dir build/master-tests --output-on-failure

cmake -S project2_pc_sim -B build/pc-tests \
  -DBUILD_LINUX_APP=ON -DBUILD_QT5_GUI=OFF -DBUILD_TESTING=ON
cmake --build build/pc-tests
ctest --test-dir build/pc-tests --output-on-failure
```

测试覆盖 golden encode、解析恢复、超长长度和 EV1527 长度边界。未覆盖 STM32 Timer/Delay 精度、RF_Tx 电气波形和真实 UART 丢字节。
