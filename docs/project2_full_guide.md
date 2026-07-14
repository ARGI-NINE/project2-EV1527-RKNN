# Project2 端到端指南

## 1. 读者、目标与前置条件

本文面向第一次接手工程的开发者。完成后，你应能在主机上验证协议/解码，在 PC 上回放 WAV，理解板端 RF 与视觉链路，并知道哪些结果必须在真实硬件上确认。

按任务准备环境：

- 主机契约：CMake 3.15+、C/C++ 编译器、CTest、Python 3；构建 Linux 网关还需 libmosquitto 开发包。
- PC GUI：Qt 5 Widgets、CMake；WSL 视觉桥按 `project2_pc_sim` 文档准备 Python/OpenCV。
- STM32：STM32F10x 标准外设库工程与 ARM 工具链，RF 接收模块接 PA0，USART1 PA9/PA10 与主控交叉连接且共地。
- RK3568：匹配运行内核的源码/配置、可用 serdev UART、`/dev/video*`、板端 RKNN/RGA/MPP/FFmpeg 库；外部 MQTT/RTSP 服务需自行提供。

所有命令默认从仓库根目录执行。

## 2. 先理解数据契约

STM32 把相邻边沿间隔存为 `uint16_t` 微秒。完整串口帧：

```text
AA 55 | pulse_count_lo pulse_count_hi | pulse_0_lo pulse_0_hi ... | xor
```

`pulse_count` 与每个 pulse 都是 LE16。`xor` 从两个长度字节起异或到 payload 末尾。EV1527 解码器通常需要约 50 个脉宽（同步高/低加 24 bit 的高/低），因此示例不能把 8 个 pulse 当作完整可解码事件。

代码路径：

1. `project2_hardware/Hardware/RF_Capture.c` 在 TIM2 双边沿中断中形成 pulse。
2. `RF_Capture_ProcessLoop` 从 ready queue 取帧并调用 `RF_Uart_SendFrame`；该哨兵环有 4 个后备槽、有效容量为 3 帧，满时拒绝新到帧，不会淘汰旧帧来保证 freshness。
3. `project2_master/linux_driver/rf433_drv.c` 验证同步、长度与 XOR，写入 16 帧 drop-oldest kfifo。
4. `project2_master/linux_app/rf_epoll.c` 读取 `struct rf433_frame`，`rf_decode.c` 调用 EV1527 C 解码器。
5. `main.c` 做近码合并、重复稳定与发布间隔限制，再输出 JSON；连接 broker 时同时发布 MQTT。

## 3. 主机上验证代码

### 3.1 master 的可移植测试

```bash
cmake -S project2_master -B build/master-tests \
  -DBUILD_LINUX_APP=OFF -DBUILD_QT5_GUI=OFF -DBUILD_TESTING=ON
cmake --build build/master-tests
ctest --test-dir build/master-tests --output-on-failure
```

成功标准因平台而异：Unix 上应为 2/2，通过 `rf_decode_contract` 与 `postprocess_labels_lifetime`；Windows 上只生成可移植的 `postprocess_labels_lifetime`，应为 1/1。前者验证 AA55/LE16/XOR、50-pulse 接受、49-pulse 拒绝和失败 confidence 为确定值；后者验证标签加载后保持进程级生命周期，并拒绝随后传入的冲突路径。

### 3.2 PC 协议和 Python CLI

```bash
cmake -S project2_pc_sim -B build/pc-tests \
  -DBUILD_LINUX_APP=ON -DBUILD_QT5_GUI=OFF -DBUILD_TESTING=ON
cmake --build build/pc-tests
ctest --test-dir build/pc-tests --output-on-failure

python -B -m unittest discover -s tests -p 'test*.py' -v
python -B ev1527_decode.py --help
```

PC CTest 还检查错误 XOR 后恢复、超长长度拒绝和 AA 重同步。Python 的 `--json-include-all-clusters` 只在写 JSON 时生效：默认只写 repeat-valid cluster；加该选项后保留所有 cluster，并在每项中保留 `repeat_valid` 便于分析。

示例：

```bash
python -B ev1527_decode.py --wav capture03.wav --start-sec 0 --end-sec 10 \
  --json-out build/default.json
python -B ev1527_decode.py --wav capture03.wav --start-sec 0 --end-sec 10 \
  --json-out build/all.json --json-include-all-clusters
```

若提示 `No candidate frame found`，先确认 WAV 路径和时间段，再检查采样波形；不要先用极低 confidence 阈值掩盖输入问题。

## 4. PC 回放 RF 主链路

```bash
python project2_pc_sim/python/wav_to_pulses.py \
  --wav capture03.wav --start-sec 0 --end-sec 10 \
  --out-json project2_pc_sim/sim_data/pulse.json \
  --out-txt project2_pc_sim/sim_data/pulse.txt

python project2_pc_sim/python/replay_pulse_timeline.py \
  --pulse-json project2_pc_sim/sim_data/pulse.json \
  | build/pc-tests/linux_app/rf_gateway --rf-input -
```

`wav_to_pulses.py` 把音频边沿变为 pulse frame；replay 按时间线输出二进制协议；PC `rf_gateway` 只接受 stdin (`--rf-input -`) 并输出事件 JSON。Windows/生成器的可执行文件位置可能不同，以 CMake 构建输出为准。

成功标准：进程无协议错误退出，并在足够重复、confidence 达阈值时输出 `rf_event`。无事件时同时检查：提取出的帧是否约 50 pulse、`--stable-repeat` 默认 2、`--min-publish-confidence` 默认 0.72、同码 `--publish-gap` 默认 6。

## 5. 部署 RF 板端链路

1. 按 [STM32 教程](../project2_hardware/README.md) 把 `project2_hardware` 加入现有标准外设库工程并烧录。
2. 按 [Linux 驱动教程](../project2_master/linux_driver/README.md) 配置 DTS、构建并加载 `rf433_drv.ko`。
3. 确认 `/dev/rf433` 是字符设备且当前用户有权限。
4. 构建并启动网关：

```bash
cmake -S project2_master -B build/master \
  -DBUILD_LINUX_APP=ON -DBUILD_QT5_GUI=OFF -DBUILD_TESTING=ON
cmake --build build/master
./build/master/linux_app/rf_gateway --rf-input /dev/rf433
```

master 明确只允许 `/dev/rf433`。stdout 的 envelope 总含 `type`、完整 `topic`、`mqtt_published`、`payload`；broker 不可用时仍应保留本地 JSON，MQTT 只降级而不终止 RF 处理。统计 timer 创建/注册失败只会禁用周期统计，不会放宽设备读错误处理。

## 6. 启动 Qt 与视觉链路

板端 GUI 依赖 RK3568 的本地媒体和 AI 库，不能用普通 PC 构建成功来替代目标验证。用匹配 toolchain/sysroot 配置并构建：

```bash
cmake -S project2_master -B build/master-board \
  -DBUILD_LINUX_APP=ON -DBUILD_QT5_GUI=ON -DBUILD_TESTING=ON
cmake --build build/master-board
```

随后运行：

```bash
./build/master-board/qt_gui/rf_dashboard_qt5 \
  --rf-input /dev/rf433 \
  --vision-device /dev/video9 \
  --vision-rtsp-url rtsp://192.168.30.26:8554/rk3568-001/cam0
```

也可用 `--disable-vision-rtsp` 关闭推流支路，或把 `--vision-device` 指向可读本地视频文件。程序会拒绝非 `/dev/video*` 且不可读的文件路径。

视觉 publish 条件：帧成功转为 RGB 且 `detGroup.count > 0` 才发 `vision/detection`；编码器打开失败时，每次重试都可能向 retained `stream/status` 发布 `state="offline"`、`reason="open_failed"`，两个字段不能合并成 `state="open_failed"`。

## 7. MQTT 与 RTSP 验收

固定默认值由代码定义：broker `192.168.30.26:1883`，device `rk3568-001`，根 topic `argi/device/rk3568-001`；RTSP URL 为 `rtsp://192.168.30.26:8554/rk3568-001/cam0`。具体订阅、retain 和故障恢复见 [MQTT 教程](../project2_master/docs/mqtt_publish_tutorial_zh.md)，推流观察见 [RTSP 教程](../project2_master/docs/rtsp_push_tutorial_zh.md)。

## 8. 排错顺序

1. 没有 UART 数据：检查 PA0 输入、PA9 TX、9600 8N1、共地和逻辑分析仪波形。
2. 驱动 `crc_err` 增长：这里指 legacy 名称下的 XOR 不匹配；检查串口丢字节、端序和帧边界。
3. `/dev/rf433` 无法打开：检查 serdev 绑定、字符设备权限和 DTS compatible。
4. 有帧无事件：检查 pulse 数、decoder confidence、稳定重复与 publish gap。
5. `mqtt_published=false`：先看本地 JSON，再检查 broker 地址、ACL 和网络；不要把本地处理成功误判成 MQTT 成功。
6. 视觉在线但无 detection topic：确认 RGB 转换成功且确实有目标。
7. RTSP 不可播放：区分编码器 open、进程内 FFmpeg/libavformat 输出、服务器写权限和客户端缓存；GUI 本地推理仍可能正常。

## 9. 清理与验证边界

停止用户态进程用 `Ctrl+C`，卸载驱动前先停止所有 `/dev/rf433` 使用者，再执行 `sudo rmmod rf433_drv`。构建目录和 `project2_pc_sim/sim_data` 是生成内容，可直接重建，不应提交。

当前主机验证不覆盖 STM32 中断时序、真实 UART 电气、内核 ABI 与目标内核绑定、RK3568 零拷贝/媒体/AI 运行、真实 broker 重连/retain、真实 RTSP server 接收。完成部署必须按对应教程在目标环境复验。
