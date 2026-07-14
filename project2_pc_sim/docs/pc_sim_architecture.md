# Project2 PC Simulator 架构

## 目的与非目标

PC simulator 用真实 WAV 和可控 TCP 数据验证协议、解码、Qt 展示与错误处理。它不加载 RK3568 kernel driver，不运行板端 RGA/RKNN/MPP/RTSP，也不发布 master MQTT。

## RF 链路

```text
WAV
 -> python/wav_to_pulses.py
 -> pulse_runtime.json
 -> python/replay_pulse_timeline.py（按时间、AA55/LE16/XOR binary stdout）
 -> rf_gateway --rf-input -
 -> NDJSON {addr,key,conf,src,pulses,seq}
 -> Qt RFGatewayClient / DashboardBackend
```

Qt fixed-chain 要求 `--wav-input`。它先对完整 WAV 运行 stage-1、在系统 temp 的 `project2_pc_sim_runtime` 写 `pulse_runtime.txt/json`，随后以 `--stable-repeat 1 --min-publish-confidence 0.92` 启动 gateway，并把 replay stdout 接到 gateway stdin。`--wav-speed` 必须 > 0，`--wav-loop` 循环时间线。

PC gateway 的默认 CLI 单独运行时是 stable repeat 2、window 12、near bits 4、min confidence 0.72、publish gap 6；Qt fixed-chain 显式覆盖其中两项。

## Vision 链路

```text
WSL source + ONNX/OpenCV pipeline
 -> wsl_vision_bridge_server.py
 -> TCP newline JSON + base64 JPEG（default port 17655）
 -> Windows Qt VisionPage
```

bridge `--source` 必填，`--model` 为空时尝试仓库根 `yolov5s.onnx`。Qt 的 `--vision-port 0` 默认禁用桥；启用时显式传 host/port。它与 master `VisionRuntime` 的 RKNN/RGA/MPP 链不同，只用于 PC UI/数据流验证。

`fake_detection_stream_test.py` 可在默认 17656 生成/serve synthetic detection；它不是 WSL real bridge 的默认 17655，也不证明模型输出。

## 构建结构

Windows 将 `rf_common` 与 `rf_gateway_core` 建为 OBJECT libraries 并直接放入 executable，避免静态库链接顺序问题；非 Windows 使用 STATIC libraries 并链接。Qt target 需要 Core/Gui/Widgets/Network，Windows 额外链接 psapi。

## 状态与边界

- `stable_group.hits` 是饱和 `uint32_t`。
- parser 保留同步/长度/XOR/恢复与 null/capacity 检查。
- GUI 负责启动、timeout、terminate/kill 和 process diagnostics；没有无用的 normalization reason/client-name bookkeeping。
- `sim_data/`、temp runtime、虚拟环境与 cache 是生成内容，不进入 Git。

## 验证

CTest 覆盖 encode/decode parity、AA resync、bad-XOR recovery、oversized length 和 null boundaries。fake event/vision scripts覆盖 schema/页面输入；没有永久测试覆盖完整 WAV 时序、Qt 交互或真实 WSL ONNX runtime。运行教程见 [../README.md](../README.md)。
