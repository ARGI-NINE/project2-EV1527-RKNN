# PC Simulator 阅读与复现路线

## 1. 前置条件

CMake 3.15+、C/C++ 编译器、Python 3。GUI 需要 Qt5 Core/Gui/Widgets/Network；WAV 提取依赖根目录 decoder 使用的 Python 数值/音频环境；WSL bridge 还需 OpenCV 与当前 vision pipeline 依赖。

## 2. 先跑协议测试

```bash
cmake -S project2_pc_sim -B build/pc-read \
  -DBUILD_LINUX_APP=ON -DBUILD_QT5_GUI=OFF -DBUILD_TESTING=ON
cmake --build build/pc-read
ctest --test-dir build/pc-read --output-on-failure
```

预期 `rf_protocol_contract` 通过。先读 `tests/rf_protocol_contract_test.c`，再读 `common/rf_protocol.c`，建立 AA55/LE16/XOR 和恢复契约。

## 3. 手工复现 RF

```bash
python project2_pc_sim/python/wav_to_pulses.py \
  --wav capture03.wav --start-sec 0 --end-sec 10 \
  --out-json project2_pc_sim/sim_data/pulse.json \
  --out-txt project2_pc_sim/sim_data/pulse.txt

python project2_pc_sim/python/replay_pulse_timeline.py \
  --pulse-json project2_pc_sim/sim_data/pulse.json --speed 1.0 \
  | build/pc-read/linux_app/rf_gateway --rf-input -
```

可执行文件路径因 generator/平台不同而变化，以构建输出为准。成功时 gateway 在满足 confidence/stability 后打印一行 `addr/key/conf/src/pulses/seq` JSON；没有事件时先检查 pulse JSON 是否有约 50-pulse frame，再调参数。

阅读顺序：`wav_to_pulses.py -> replay_pulse_timeline.py -> rf_source.c -> rf_epoll.c -> common/rf_protocol.c -> rf_decode*.c -> main.c`。

## 4. 运行 Qt fixed-chain

先启用 GUI 构建：

```bash
cmake -S project2_pc_sim -B build/pc-gui \
  -DBUILD_LINUX_APP=ON -DBUILD_QT5_GUI=ON -DBUILD_TESTING=ON
cmake --build build/pc-gui
ctest --test-dir build/pc-gui --output-on-failure
```

运行示例：

```bash
./build/pc-gui/qt_gui/rf_dashboard_qt5 \
  --gateway ./build/pc-gui/linux_app/rf_gateway \
  --wav-input ./capture03.wav \
  --python-bin python \
  --wav-speed 1.0
```

Windows 使用 `.exe` 并按实际 generator 调整路径。GUI 会自行执行完整 WAV preprocessing、创建 temp pulse files、启动 gateway/replay。成功标准：RF 状态为 `proc://rf_gateway/stdin`，首个事件在 timeout 内出现，波形/日志更新。

## 5. Vision bridge

在 WSL 明确给 source：

```bash
python project2_pc_sim/python/wsl_vision_bridge_server.py \
  --host 0.0.0.0 --port 17655 \
  --source /path/to/video.mp4 \
  --model /path/to/yolov5s.onnx
```

Windows Qt 增加：

```text
--vision-host <WSL reachable address> --vision-port 17655
```

默认 port 0 是禁用，不会自动猜测 WSL。成功标准：bridge connected、camera/model online、frame/FPS/overlay 更新。`fake_detection_stream_test.py --serve` 默认 17656，只验证 Qt schema/显示，不证明 ONNX 推理。

## 6. 排错和清理

- gateway not found：显式 `--gateway`。
- WAV missing/prepare timeout：核对路径、Python 与依赖，手工运行 stage-1。
- `--wav-speed` <= 0：使用正数。
- no valid pulse frames：检查 WAV 区段、stage-1 filter 和输出 JSON。
- bridge refused：检查 bind host、WSL/Windows 地址、防火墙和端口是否一致。
- JPEG/JSON error：先运行 fake server，分离 network/schema 与 model 问题。

关闭 GUI 会 terminate/必要时 kill 子进程。删除 build、`sim_data/` 和系统 temp `project2_pc_sim_runtime` 即可清理生成物；不要提交虚拟环境/cache/output。

## 7. 验证边界

CTest 不覆盖 Qt 交互、完整长 WAV、WSL network、ONNX 精度或板端 RK3568 runtime。fake scripts 是局部 UI/schema 验证，不能替代硬件/模型证据。
