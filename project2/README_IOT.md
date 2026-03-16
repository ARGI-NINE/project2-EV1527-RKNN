# Project2 IoT Gateway

本目录新增了完整的 IoT 项目工程骨架（不涉及 `project1`）：

```
project2
├── CMakeLists.txt
├── common
│   ├── rf_protocol.h
│   └── rf_protocol.c
├── docs
│   └── project2_iot_design.md
├── linux_app
│   ├── main.c
│   ├── rf_epoll.c/h
│   ├── rf_decode.c/h
│   ├── rf_decode_c.c/h
│   ├── rf_db.c/h
│   ├── rf_mqtt.c/h
│   └── rf_source.c/h
├── linux_driver
│   └── rf433_drv.c
├── simulator
│   ├── CMakeLists.txt
│   └── rf_simulator.c
├── stm32_firmware
│   ├── rf_capture.c/h
│   ├── rf_uart.c/h
│   ├── rf_tx.c/h
│   └── main_sim.c
├── python
│   ├── wav_to_pulses.py
│   ├── ev1527_decode_bridge.py
│   └── run_virtual_pipeline.py
└── qt_gui
    └── mainwindow.cpp
```

## 快速虚拟联调

1) WAV 抽脉冲并解码验证

```powershell
.\.venv\Scripts\python.exe .\project2\python\run_virtual_pipeline.py --wav .\capture04.wav --start-sec 0
```

2) 仅生成模拟器输入

```powershell
.\.venv\Scripts\python.exe .\project2\python\wav_to_pulses.py --wav .\capture04.wav --out-txt .\project2\sim_data\pulse.txt --out-json .\project2\sim_data\pulse.json
```

3) Linux 侧链路（目标板）

- `rf_simulator` 输出协议流
- `rf_gateway` 读取 `rf_fd`，经 `epoll` 解码并入 SQLite

## 说明

- 原有 `project2/ev1527_decode.py` 保留不改，用于深度 WAV 解码分析。
- 新增 `python/ev1527_decode_bridge.py` 提供 `pulse.json -> {"addr","key"}` 的轻量接口，便于 C 程序调用。
- 新增 `linux_app/rf_decode_c.c` 为 C 转写版本，可直接用于嵌入式部署。


## Online Real-time Additions

New online modules were added without removing offline flow:

- STM32 online capture API in `stm32_firmware/rf_capture.c/h`
  - `rf_capture_online_init(...)`
  - `rf_capture_process_ccr(...)`
  - `rf_capture_process_dma(...)`
  - `rf_capture_set_filter(...)`
- STM32 online simulation entry: `stm32_firmware/main_online_sim.c`
- Linux multi-frame consensus decoder:
  - `linux_app/rf_decode_window.c/h`
  - integrated in `linux_app/main.c` (default `--decode-mode window`)

Runtime options for real-time decode:

```powershell
.\project2\build\linux_app\rf_gateway.exe --rf-input .\project2\sim_data\rf_stream.bin --decode-mode window --window-size 12 --stable-repeat 3 --min-win-confidence 0.60 --publish-gap 6
```

This keeps existing offline tools unchanged:

- `project2/ev1527_decode.py`
- `project2/python/wav_to_pulses.py`
- `project2/python/run_virtual_pipeline.py`
