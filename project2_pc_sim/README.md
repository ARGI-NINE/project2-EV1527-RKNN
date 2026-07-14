# Project2 PC Simulator 教程

## 目标

在没有 STM32、`/dev/rf433` 和 RK3568 媒体栈时，用真实 WAV 验证 RF 协议/解码/Qt 数据流，并可用 WSL TCP bridge 验证 Vision 页面。PC simulator 不发布 master MQTT，也不等价于板端 VisionRuntime。

## 前置条件

- CMake 3.15+、C/C++ 编译器、Python 3。
- GUI：与编译器匹配的 Qt5 Core/Gui/Widgets/Network。
- RF：可读 WAV 与 Python decoder/extractor 依赖。
- Vision：WSL Python/OpenCV/vision pipeline、明确的视频 source 和 model。

可移植环境变量见 [docs/toolchain_environment.txt](docs/toolchain_environment.txt)。

## 构建和 CTest

无 GUI：

```bash
cmake -S project2_pc_sim -B build/pc \
  -DBUILD_LINUX_APP=ON -DBUILD_QT5_GUI=OFF -DBUILD_TESTING=ON
cmake --build build/pc
ctest --test-dir build/pc --output-on-failure
```

GUI：

```bash
cmake -S project2_pc_sim -B build/pc-gui \
  -DBUILD_LINUX_APP=ON -DBUILD_QT5_GUI=ON -DBUILD_TESTING=ON \
  -DCMAKE_PREFIX_PATH=/path/to/qt
cmake --build build/pc-gui
ctest --test-dir build/pc-gui --output-on-failure
```

Windows generator 的 executable 通常带 `.exe`。当前 CMake 在 Windows 使用 OBJECT libraries，在非 Windows 使用 STATIC libraries；两条分支都应生成 `rf_gateway` 和 `rf_protocol_contract_test`。CTest 成功标准是 `rf_protocol_contract` 通过。

## 手工 RF 回放

```bash
python project2_pc_sim/python/wav_to_pulses.py \
  --wav capture03.wav --start-sec 0 --end-sec 10 \
  --out-json project2_pc_sim/sim_data/pulse.json \
  --out-txt project2_pc_sim/sim_data/pulse.txt

python project2_pc_sim/python/replay_pulse_timeline.py \
  --pulse-json project2_pc_sim/sim_data/pulse.json --speed 1.0 \
  | build/pc/linux_app/rf_gateway --rf-input -
```

`wav_to_pulses` 只做 MCU-like candidate filtering，不给 repeat/confidence 结论；gateway 用 C decoder、stable group 与 publish gap 输出：

```json
{"addr":"0x35A1BC","key":"12","conf":0.94,"src":"c","pulses":50,"seq":7}
```

输出是结构示例。没有事件时检查 pulse JSON 是否有约 50-pulse candidate、confidence、stable repeat 和 replay pipe 是否为 binary-safe。

## Qt fixed-chain

```bash
./build/pc-gui/qt_gui/rf_dashboard_qt5 \
  --gateway ./build/pc-gui/linux_app/rf_gateway \
  --wav-input ./capture03.wav \
  --python-bin python \
  --wav-speed 1.0
```

可加 `--wav-loop`。GUI 会处理完整 WAV，写系统 temp `project2_pc_sim_runtime/pulse_runtime.*`，以 `stable-repeat=1`、`min-publish-confidence=0.92` 启动 gateway。`--wav-input` 必填，speed 必须大于 0。

## Offline cluster JSON

根目录工具默认只写 repeat-aware 过滤结果；include-all 已有实际行为：

```bash
python -B ev1527_decode.py --wav capture03.wav --json-out build/default.json
python -B ev1527_decode.py --wav capture03.wav --json-out build/all.json \
  --json-include-all-clusters
python -B -m unittest discover -s tests -p 'test*.py' -v
```

all-cluster JSON 每项的 `repeat_valid` 表示是否满足重复条件。

## WSL Vision bridge

WSL：

```bash
python project2_pc_sim/python/wsl_vision_bridge_server.py \
  --host 0.0.0.0 --port 17655 \
  --source /path/to/video.mp4 --model /path/to/yolov5s.onnx
```

Qt 增加 `--vision-host <reachable-host> --vision-port 17655`。GUI 默认 vision port 0，即禁用。bridge 发送 NDJSON snapshot 和 base64 JPEG；它不是 RKNN/RGA/MPP/RTSP。

局部 UI 测试：`fake_detection_stream_test.py --serve` 默认端口 17656；`fake_event_record_test.py` 生成/校验 event record。两者是 synthetic schema 工具，不证明模型、硬件或端到端行为。

## 排错与清理

- CMake 找不到 Qt：设置匹配编译器的 `CMAKE_PREFIX_PATH`。
- gateway/WAV/Python 找不到：显式传三个对应 CLI 路径。
- first event timeout：先手工运行 WAV 两阶段，检查 candidates。
- Vision disconnected：核对 WSL bind、Windows 可达地址、防火墙与端口。
- JSON/JPEG 错误：先用 fake server 分离 schema/network 与真实 pipeline。

关闭 GUI 后子进程会 terminate，超时才 kill。删除 build、`sim_data/`、系统 temp runtime、`.venv`/cache 即清理；这些不应提交。

架构与字段边界见 [docs/pc_sim_architecture.md](docs/pc_sim_architecture.md) 和 [docs/ev1527_truth_mapping.md](docs/ev1527_truth_mapping.md)。
