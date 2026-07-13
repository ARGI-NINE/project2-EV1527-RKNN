# project2 总览与阅读地图

## 1. 这份文档负责什么

这份文档是整个仓库的总入口，不替代各子目录的专题深读。它只做四件事：

1. 统一项目定位。
2. 讲清楚当前代码里到底已经落了哪些主链。
3. 明确本次完成口径只到“静态代码要求与静态代码检验”。
4. 给出一条从顶层到专题、再到具体代码的阅读路线。

如果你需要的是逐函数讲解，请直接跳到后面的专题文档，而不是停留在本页。

## 2. 统一项目定位

`project2` 当前统一定位为“基于 RK3568 的多源事件感知与视频留证网关”。

这个定位要拆成三层来理解：

- “多源事件感知”表示它不是只围绕一个 RF433 解码器组织代码，而是围绕“事件源接入”组织代码。
- “视频留证”表示视觉链路不是装饰页面，而是项目里独立存在的一条本地运行链。
- “网关”表示它关注的是接入、归一化、展示、发布与扩展接口，不是门禁控制器本体。

从当前代码看，真正落地的事件源只有两类：

- 已落地：RF433/EV1527 前端适配链。
- 已落地：板端本地视觉检测链。

另外还有两类只到规划或保留位：

- 规划中：MQTT command 输入。
- 保留位：GPIO 干接点输入。

## 3. 本次完成口径

这一节非常关键，后续所有文档都以这里为准。

### 3.1 可以确认什么

当前可以通过静态代码直接确认的内容包括：

- 目录职责分工。
- 模块之间的数据流向。
- 驱动、用户态、Qt、本地视觉运行时的入口函数和依赖关系。
- RF 帧协议的字节格式。
- 当前代码里哪些功能是主链，哪些只是辅助模块或扩展位。

### 3.2 不能宣称什么

当前不能因为代码存在，就直接宣称下面这些事情已经完成：

- STM32 采集板与 RK3568 板卡的实机接线和长期稳定联调。
- `project2_hardware -> UART -> serdev -> /dev/rf433 -> rf_gateway -> Qt` 的整机验收。
- 视觉检测与事件录制的整机闭环。
- MQTT 命令下发闭环。
- GPIO 干接点实机接入闭环。

所以，这次文档的正确口径只能是：

> 已完成静态代码要求与静态代码检验，不宣称实机联调完成。

## 4. 三个子项目分别负责什么

| 子项目 | 你可以把它理解成什么 | 当前代码里的核心职责 | 明确不该写成什么 |
| --- | --- | --- | --- |
| `project2_hardware` | RF 前端适配固件 | 采脉冲、分帧、编码、串口上送 | 不是门禁控制器，不是最终解码展示层 |
| `project2_master` | RK3568 板端网关主体 | 驱动接入、用户态解析、Qt 看板、本地视觉运行时 | 不是离线仿真器 |
| `project2_pc_sim` | PC 侧离线回放与参考环境 | WAV 回放、WSL 视觉桥接、回归参考 | 不是板端运行路径，不是验收真值源 |

## 5. 当前代码里的两条主链

## 5.1 RF433 事件主链

这是当前仓库里最明确、也最适合从下往上追的一条链。

```text
RF 前端脉冲
-> project2_hardware/Hardware/RF_Capture.c
-> project2_hardware/Hardware/RF_Protocol.c
-> project2_hardware/Hardware/RF_Uart.c
-> RK3568 串口
-> project2_master/linux_driver/rf433_drv.c
-> /dev/rf433
-> project2_master/linux_app/main.c
-> JSON envelope / MQTT
-> project2_master/qt_gui
```

这条链里每一层分别做什么：

- STM32 固件把离散的脉冲宽度整理成 `rf_frame_t`，再编码成 `AA55 + LEN + PAYLOAD + CRC`。
- RK3568 驱动把串口字节流重新拼成帧，导出为 `/dev/rf433`。
- `rf_gateway` 从 `/dev/rf433` 读帧、做 EV1527 解码、做稳定分组和去重、再输出 JSON。
- Qt 侧消费这些 JSON，并把状态和波形显示出来。

这里要特别记住：Qt 不是 RF 数据的生产者，只是消费和展示层。

这一节不是抽象总结，下面几段源码就是主链的真实锚点。

摘自 `project2_hardware/Hardware/RF_Protocol.c`：

```c
bytes = (uint16_t)(frame->len * 2u);
total = (size_t)2u + 2u + bytes + 1u;

out[0] = RF_PROTO_SYNC0;
out[1] = RF_PROTO_SYNC1;
out[2] = (uint8_t)(frame->len & 0xFFu);
out[3] = (uint8_t)((frame->len >> 8u) & 0xFFu);
```

这段代码直接证明 hardware 上送协议的起点就是 `AA55 + LEN`，并且 `LEN` 表示 pulse 个数而不是 payload 字节数。文档里把共享 ABI 写成 `AA55/LEN/PAYLOAD/XOR`，依据就在这里。

摘自 `project2_master/linux_driver/rf433_drv.c`：

```c
memset(&frame, 0, sizeof(frame));
frame.timestamp_ns = ktime_get_real_ns();
frame.pulse_count  = priv->expected_pulses;
frame.seq          = ++priv->seq;

for (i = 0; i < priv->expected_pulses; i++) {
    u16 lo = priv->payload_buf[i * 2];
    u16 hi = priv->payload_buf[i * 2 + 1];
    frame.pulse[i] = lo | (hi << 8);
}
```

这里说明 `/dev/rf433` 之前的驱动层不仅把 UART payload 重新拼回 `pulse[]`，还新增了 `timestamp_ns` 和驱动侧 `seq`。所以 `timestamp_ns` 不是 hardware wire ABI 自带字段，而是 master driver 层新增的元数据。

摘自 `project2_master/linux_app/main.c`：

```c
"\"addr\":\"%s\","
"\"key\":\"%s\","
"\"conf\":%.4f,"
"\"confidence\":%.4f,"
"\"src\":\"%s\","
"\"source\":\"%s\","
"\"seq\":%u,"
"\"drv_seq\":%u,"
"\"timestamp_ns\":%llu,"
"\"decode_us\":%llu,"
"\"mqtt_connected\":%s,"
"\"pulse_count\":%u,"
"\"pulse_us\":[",
```

这一段则说明真正面向 Qt 和 MQTT 的业务字段是在 `rf_gateway` 用户态里组装的。也正因为如此，本总览把 `addr` / `key` / `conf` 明确归到 `project2_master/linux_app` 之后，而不反写回 hardware 文档。

## 5.2 板端本地视觉主链

第二条主链在 `project2_master` 内部，重点不是“网页服务”，而是本地运行时。

```text
/dev/video* 或本地视频文件
-> VisionRuntime
-> AI 输入池
-> RKNN 推理
-> 推理后流分支
-> VisionSnapshot
-> Qt 视觉页面
-> detection MQTT / RTSP 状态与推流支线
```

从代码事实看，这条链至少可以确认下面几点：

- 默认视觉输入是 `/dev/video9`。
- `--vision-device` 也接受一个可读的本地视频文件。
- 本地视觉运行时在 Qt 进程内，不依赖 `pc_sim`。
- RTSP 是视觉链的输出支线之一，但本文不展开其协议细节。

摘自 `project2_master/qt_gui/vision/vision_runtime.cpp`：

```cpp
const QString modelPath = resolveModelPath();
const QString inputPath = options_.visionDevice.trimmed();
const bool useV4L2 = isAllowedVisionDevicePath(inputPath);

if (modelPath.isEmpty()) {
    backend_->addLog(
        "ERROR",
        "VISION",
        QStringLiteral("VisionRuntime could not find a usable RKNN model inside project2_master assets")
    );
    running_.store(false);
    return;
}

rknnPool<rkYolov5s> aiPool(modelPath.toStdString(), kAiWorkerThreads, kAiQueueSize);
if (aiPool.init() != 0) {
    backend_->addLog("ERROR", "VISION", QStringLiteral("VisionRuntime model init failed: %1").arg(modelPath));
    running_.store(false);
    return;
}
```

这段代码可以直接支撑“板端本地视觉运行时在 Qt 进程内”这个结论：它不是去连 `pc_sim` bridge，而是就地解析 `--vision-device`、定位 `project2_master` 里的模型文件，并创建 `rknnPool<rkYolov5s>` 做本地推理。

MQTT / RTSP 专题如果要继续深入，请转到：

- [../project2_master/docs/project2_iot_design.md](../project2_master/docs/project2_iot_design.md)
- [../project2_master/docs/rk3568_vision_dashboard.md](../project2_master/docs/rk3568_vision_dashboard.md)

## 6. 当前代码中的“已实现 / 保留 / 规划”总表

| 能力项 | 现状 | 依据的代码事实 | 文档应如何表述 |
| --- | --- | --- | --- |
| STM32 采脉冲并 UART 上送 | 已实现 | `project2_hardware/User/main.c`、`RF_Capture.c`、`RF_Uart.c` | 可以写成已落地主链 |
| RK3568 通过 serdev 导出 `/dev/rf433` | 已实现 | `project2_master/linux_driver/rf433_drv.c` | 可以写成已落地主链 |
| `rf_gateway` 读取 `/dev/rf433` 并输出 JSON | 已实现 | `project2_master/linux_app/main.c` | 可以写成已落地主链 |
| Qt 消费 RF JSON 并展示 | 已实现 | `project2_master/qt_gui/rf/*` | 可以写成已落地主链 |
| 板端本地视觉运行时 | 已实现 | `project2_master/qt_gui/vision/vision_runtime.cpp` | 可以写成已落地主链 |
| MQTT 发布 | 已实现但非本文重点 | `linux_app/mqtt_publisher.c`、`vision_runtime.cpp` | 只点到为止，细节外链 |
| RTSP 推流支线 | 已实现但非本文重点 | `vision_runtime.cpp`、`mpp_encoder_rtsp.*` | 只点到为止，细节外链 |
| MQTT command 输入 | 未完成 | 当前代码无命令订阅与处理闭环 | 必须写成规划 / TODO |
| GPIO 干接点输入 | 未完成 | 仓库当前无落地实现 | 必须写成预留位 |
| 事件触发录制闭环 | 未完成 | 只有接口规划，没有完整实装闭环 | 必须写成 TODO |
| 实机联调完成 | 未确认 | 仓库无法靠静态代码证明 | 不得宣称 |

## 7. 顶层阅读路线

## 7.1 推荐给第一次接手仓库的人

1. 先读本页，建立整体地图。
2. 再读 [project2_hardware_deep_dive.md](project2_hardware_deep_dive.md)，确认下位机链路的真实边界。
3. 然后去 [../project2_master/docs/project2_master_reading_guide_zh.md](../project2_master/docs/project2_master_reading_guide_zh.md)，继续追板端主控侧。
4. 最后再按需要看 `pc_sim` 文档，避免一上来把仿真路径误当成板端路径。

## 7.2 如果你只关心硬件链路

按下面顺序读：

1. [project2_hardware_deep_dive.md](project2_hardware_deep_dive.md)
2. [project2_hardware_deep_dive_01_overview.md](project2_hardware_deep_dive_01_overview.md)
3. [project2_hardware_deep_dive_02_capture_and_uart.md](project2_hardware_deep_dive_02_capture_and_uart.md)
4. [project2_hardware_deep_dive_03_protocol_timer_and_aux.md](project2_hardware_deep_dive_03_protocol_timer_and_aux.md)
5. [project2_hardware_deep_dive_04_function_index.md](project2_hardware_deep_dive_04_function_index.md)

## 7.3 如果你只关心 RK3568 板端软件

按下面顺序读：

1. [../project2_master/docs/project2_master_reading_guide_zh.md](../project2_master/docs/project2_master_reading_guide_zh.md)
2. [../project2_master/docs/project2_shared_protocol_deep_dive.md](../project2_master/docs/project2_shared_protocol_deep_dive.md)
3. [../project2_master/docs/project2_master_driver_deep_dive.md](../project2_master/docs/project2_master_driver_deep_dive.md)
4. [../project2_master/docs/project2_master_userland_deep_dive.md](../project2_master/docs/project2_master_userland_deep_dive.md)
5. [../project2_master/docs/project2_master_qt_vision_deep_dive.md](../project2_master/docs/project2_master_qt_vision_deep_dive.md)

## 7.4 如果你只关心仿真与对照

直接读：

- [../project2_pc_sim/docs/project2_pc_sim_reading_guide.md](../project2_pc_sim/docs/project2_pc_sim_reading_guide.md)
- [../project2_pc_sim/docs/project2_pc_sim_function_index.md](../project2_pc_sim/docs/project2_pc_sim_function_index.md)
- [../project2_pc_sim/docs/pc_sim_architecture.md](../project2_pc_sim/docs/pc_sim_architecture.md)

## 8. 阅读时最容易踩的三个误区

### 误区 1：把 `pc_sim` 当成板端主路径

不是。`pc_sim` 是离线仿真和回归参考。板端主路径在 `project2_master`。

### 误区 2：把 Qt 当成 RF 主链的生产层

不是。RF 主链的关键生产层是 `project2_hardware`、`rf433_drv` 和 `rf_gateway`。Qt 在 RF 侧主要是消费层，在视觉侧才同时承担本地运行时宿主角色。

### 误区 3：把“代码里有支线”理解成“实机已经闭环”

不能这么写。尤其是 MQTT command、GPIO 干接点、事件录制、整机联调，这些都必须按“未完成或未验证”处理。

## 9. 本页之后应该去哪里

- 想看下位机固件主线：去 [project2_hardware_deep_dive.md](project2_hardware_deep_dive.md)
- 想看协议字节格式：去 [../project2_master/docs/project2_shared_protocol_deep_dive.md](../project2_master/docs/project2_shared_protocol_deep_dive.md)
- 想看 MQTT / RTSP 专题：去 [../project2_master/docs/project2_iot_design.md](../project2_master/docs/project2_iot_design.md) 和 [../project2_master/docs/rk3568_vision_dashboard.md](../project2_master/docs/rk3568_vision_dashboard.md)
- 想看 PC 仿真：去 [../project2_pc_sim/docs/project2_pc_sim_reading_guide.md](../project2_pc_sim/docs/project2_pc_sim_reading_guide.md)
