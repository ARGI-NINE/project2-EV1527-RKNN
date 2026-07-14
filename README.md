# Project2：RF433 采集、网关与视觉看板

Project2 把 STM32 脉宽采集、RK3568 Linux 驱动/网关、Qt5 视觉看板和 PC 仿真放在同一仓库。本文是导航页；第一次运行请直接阅读 [端到端指南](docs/project2_full_guide.md)。

## 目录与数据流

| 路径 | 用途 | 运行环境 |
|---|---|---|
| `project2_hardware/` | TIM2 双边沿采集、AA55 串口封包、USART1 异步发送 | STM32F103 + 标准外设库 |
| `project2_master/linux_driver/` | serdev 字节流校验、帧队列、`/dev/rf433` 与 ioctl/sysfs | 匹配目标内核的 RK3568 Linux |
| `project2_master/linux_app/` | 读取驱动记录、EV1527 解码、稳定/去重、stdout JSON 与 MQTT | Linux + libmosquitto |
| `project2_master/qt_gui/` | 启动本机网关、显示 RF/视觉数据、板端推理与 RTSP | RK3568 + Qt5 + 板端媒体/AI 库 |
| `project2_pc_sim/` | WAV 转脉宽、stdin 回放、Windows Qt 看板、WSL 视觉桥 | Windows/WSL 或 Linux |
| `tests/` | 根目录 Python 解码器契约 | Python 3 |

RF 主链路是：接收模块 `DATA -> PA0/TIM2 -> rf_proto_encode -> PA9/USART1 -> serdev -> /dev/rf433 -> rf_gateway -> JSON/MQTT -> Qt`。视觉链路是：`/dev/video9` 或本地视频文件进入 `VisionRuntime`，经 V4L2/MPP、RGA、RKNN 后更新 Qt；RGB 转换成功且存在检测结果时才发布 detection MQTT，完成标注后的 NV12 帧可进入 RTSP 支路。

## 最短的主机验证

以下命令不需要板卡、MQTT broker 或 RTSP server：

```bash
cmake -S project2_master -B build/master -DBUILD_LINUX_APP=OFF -DBUILD_QT5_GUI=OFF -DBUILD_TESTING=ON
cmake --build build/master
ctest --test-dir build/master --output-on-failure

cmake -S project2_pc_sim -B build/pc -DBUILD_LINUX_APP=ON -DBUILD_QT5_GUI=OFF -DBUILD_TESTING=ON
cmake --build build/pc
ctest --test-dir build/pc --output-on-failure

python -B -m unittest discover -s tests -p 'test*.py' -v
```

Unix 上预期通过 master 的 `rf_decode_contract` 与 `postprocess_labels_lifetime`；Windows portable 配置只运行后者。PC 的 `rf_protocol_contract`、Python include-all JSON 测试，以及独立仓库 `../project2_test` 的 CTest 见其 [hardware/README.md](../project2_test/hardware/README.md)。

## 文档入口

- [端到端构建、运行与排错](docs/project2_full_guide.md)
- [STM32 硬件总览](docs/project2_hardware_deep_dive.md)
- [RK3568 master 总览](project2_master/README.md)
- [共享串口协议](project2_master/docs/project2_shared_protocol_deep_dive.md)
- [MQTT 发布教程](project2_master/docs/mqtt_publish_tutorial_zh.md)
- [RTSP 推流教程](project2_master/docs/rtsp_push_tutorial_zh.md)
- [PC 仿真教程](project2_pc_sim/README.md)

## 协议与边界

串口帧是 `AA 55 | pulse_count(LE16) | pulses(每项 LE16) | checksum`。校验算法只是从长度字段开始逐字节异或得到一个字节；代码为兼容历史保留 `rf_proto_crc8`、`crc_err`、`RF_ST_CRC` 等名字，它们不表示多项式 CRC，也不提供密码学完整性。

本仓库的主机验证覆盖协议、解码、标签生命周期、跨平台 CMake 和 Python CLI 行为；不等价于 STM32 烧录、目标内核模块加载、RK3568 摄像头/AI/媒体运行、真实 MQTT 重连或 RTSP 接收验证。相应文档会明确目标环境的验收步骤。

## 本地 Git 边界

`project2` 与 `../project2_test` 是两个独立 Git 仓库。虚拟环境、缓存和仿真输出不应纳入版本；外部板卡 PDF 与临时摘录也不是项目事实源。执行 Git 操作时先用 `git rev-parse --show-toplevel` 确认当前仓库，避免把两个仓库混在一次提交中。
