# project2

`project2` 当前统一定位为“基于 RK3568 的多源事件感知与视频留证网关”。

## 先看完成口径

- 本次完成口径是“静态代码要求已落库、并已按静态代码进行检视”。
- 本仓库当前文档不宣称“实机接线完成”“板级联调完成”“整机验收完成”。
- 当前可以从代码直接确认的能力边界，是“模块职责、接口形状、主调用链、配置入口、保留项和未完成项”。
- 当前不应被写成已完成的事项，包括但不限于：`project2_hardware -> RK3568 -> Qt` 的实机端到端联调、事件触发录制闭环、MQTT 命令输入闭环、GPIO 干接点接入闭环。

## 项目定位

这不是一个“单纯的 RF433 解码实验”，也不是“门禁控制器替代品”。从仓库里的现有代码来看，它更准确的定位是：

- 在 RK3568 板端汇聚多种事件源。
- 对事件做归一化、展示和后续发布。
- 把视觉链路接进来，为事件提供视频留证基础能力。
- 保留 MQTT 命令、GPIO 干接点、事件录制等扩展位，但不把这些规划项写成现状。

同样需要明确边界：

- 不宣称开门授权能力。
- 不宣称遥控器破解、学习或替代控制器能力。
- 不把 `project2_pc_sim` 写成板端实时运行路径。

## 仓库结构

| 目录 | 角色 | 当前代码事实 |
| --- | --- | --- |
| `project2_hardware/` | STM32 前端固件 | 负责 RF 脉冲采集、分帧、协议编码、UART 上送 |
| `project2_master/` | RK3568 主控侧 | 负责 UART 上层接入、`/dev/rf433`、RF 网关、Qt 看板、本地视觉运行时 |
| `project2_pc_sim/` | PC 离线仿真侧 | 负责 WAV 回放、WSL 视觉桥接、离线对照，不是板端部署路径 |
| `docs/` | 本层总览文档 | 负责整个仓库的阅读地图，以及 `project2_hardware` 专题阅读入口 |

## 当前能从代码确认的两条主链

### 1. RF433 事件链

```text
RF 前端脉冲
-> STM32 TIM2 输入捕获
-> RF_Capture 分帧
-> RF_Protocol 编码
-> USART1 发送
-> RK3568 串口
-> serdev 驱动
-> /dev/rf433
-> rf_gateway
-> Qt / MQTT
```

这条链路的代码入口分散在两个子项目里：

- 下位机侧：`project2_hardware/User/main.c`
- 主控侧：`project2_master/linux_driver/rf433_drv.c`
- 用户态：`project2_master/linux_app/main.c`

可以直接托底这条结论的源码入口如下。

摘自 `project2_hardware/User/main.c`：

```c
int main(void) {
    RF_Capture_Init();
    RF_Uart_Init();
    Timer_Init();

    while (1) {
        RF_Capture_ProcessLoop();
    }
}
```

这段入口代码说明硬件侧当前运行态就是“采集初始化 -> UART 初始化 -> 1ms 心跳 -> 主循环冲刷帧”。它没有 RTOS 任务编排，也没有在 STM32 侧生成 `addr` / `key` / `conf` 这类业务字段。

摘自 `project2_master/linux_app/main.c`：

```c
appendf(
    payload,
    capacity,
    &offset,
    "{"
    "\"device_id\":\"%s\","
    "\"type\":\"rf_event\","
    "\"rf_input\":\"%s\","
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

这里可以直接看到 `addr`、`key`、`conf`、`source`、`seq`、`drv_seq`、`timestamp_ns`、`pulse_us[]` 都是在 master 用户态网关里拼进 JSON 的，所以 README 才会把它们归到 `project2_master`，而不是 `project2_hardware` 的 UART wire ABI。

### 2. 本地视觉链

```text
/dev/video* 或本地视频文件
-> VisionRuntime
-> RKNN / RGA / MPP 相关处理
-> VisionSnapshot
-> Qt 视觉页面
-> detection MQTT / RTSP 支线
```

这里的重点是：当前代码里视觉运行时位于 `project2_master/qt_gui/vision/vision_runtime.cpp`，它是板端本地运行时，不是 `pc_sim` 的桥接路径。

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

这段代码说明板端视觉链确实在 Qt 进程内直接定位 RKNN 模型、检查输入源并创建 `rknnPool<rkYolov5s>`。因此顶层 README 把它定义成 `project2_master` 的本地视觉主链，而不是 `project2_pc_sim` 的 WSL bridge 支路。

## 推荐阅读路线

1. 先读 [docs/project2_full_guide.md](docs/project2_full_guide.md)，先把整个仓库的职责边界看对。
2. 如果你先关心下位机链路，进入 [docs/project2_hardware_deep_dive.md](docs/project2_hardware_deep_dive.md)。
3. 按硬件专题顺序继续读：
   [01 总览](docs/project2_hardware_deep_dive_01_overview.md) ->
   [02 采集与 UART 主线](docs/project2_hardware_deep_dive_02_capture_and_uart.md) ->
   [03 协议、时基与辅助模块](docs/project2_hardware_deep_dive_03_protocol_timer_and_aux.md) ->
   [04 函数索引](docs/project2_hardware_deep_dive_04_function_index.md)
4. 如果你要继续追到 RK3568 板端，转到 [project2_master/docs/project2_master_reading_guide_zh.md](project2_master/docs/project2_master_reading_guide_zh.md)。
5. 如果你只想看 MQTT / RTSP 相关专题，不在本层文档展开，直接去：
   [project2_master/docs/project2_iot_design.md](project2_master/docs/project2_iot_design.md)
   和
   [project2_master/docs/rk3568_vision_dashboard.md](project2_master/docs/rk3568_vision_dashboard.md)
6. 如果你要看离线回放和仿真，再读 [project2_pc_sim/docs/project2_pc_sim_reading_guide.md](project2_pc_sim/docs/project2_pc_sim_reading_guide.md)。

## 这套文档怎么理解“已实现”和“未实现”

后续文档统一按下面三种口径写：

- 已实现：代码里已经有稳定入口、调用链和数据结构。
- 已保留但未闭环：代码里有接口、支线或规划，但不能写成已交付功能。
- 不在本次口径：需要实机接线、联调、长期稳定性验证、整机验收的内容。

如果文档里出现“建议”“规划”“保留”“TODO”等字样，它们都不是本次完成项。
