# PC Simulator 当前函数/脚本索引

## CMake targets

| Target | 当前成员 |
|---|---|
| `rf_common` | `common/rf_protocol.c` |
| `rf_gateway_core` | `rf_epoll.c`、`rf_decode.c`、`rf_decode_c.c`、`rf_source.c` |
| `rf_gateway` | `main.c` + 上述对象/静态库 |
| `rf_dashboard_qt5` | app/core/log/rf/vision/widgets，Qt Core/Gui/Widgets/Network |
| `rf_protocol_contract_test` | `tests/rf_protocol_contract_test.c` + protocol |

Windows 使用 OBJECT，非 Windows 使用 STATIC linkage。

## RF C 路径

- `rf_source_open/close`：PC 只接受 `-`/stdin。
- `rf_epoll_run`、`consume_rf_stream`：读 stdin、逐字节 feed parser。
- `rf_proto_parser_init/consume`、`rf_proto_encode`：AA55/LE16/XOR。
- `rf_decode_frame`、`rf_decode_ev1527_c`：约 50-pulse EV1527。
- `stable_groups_decay/find/alloc/seed/update`：24-bit 近码合并；hits 32-bit 饱和。
- `on_rf_frame`：confidence/stability/publish gap 后输出 NDJSON。

## Python

| 脚本 | 入口/用途 |
|---|---|
| `wav_to_pulses.py` | `main`：WAV stage-1 -> pulse txt/json |
| `replay_pulse_timeline.py` | `main`：按 candidate time 输出 binary packet |
| `wsl_vision_bridge_server.py` | `main`：VisionPipeline -> TCP NDJSON/JPEG |
| `fake_detection_stream_test.py` | synthetic detection artifact/可选 TCP serve |
| `fake_event_record_test.py` | 校验/生成 RF event record artifact |

root `ev1527_decode.py` 是更完整的 offline cluster 工具，不是 GUI fixed-chain 的 stage-1 脚本。

## Qt

```text
main -> MainWindow
├─ RFGatewayClient
│  ├─ prepareRealtimeTimeline (wav_to_pulses)
│  ├─ startGatewayWithRealtimeInput
│  │  ├─ rf_gateway --rf-input -
│  │  └─ replay_pulse_timeline -> gateway stdin
│  └─ handle JSON / process diagnostics
├─ RFStatusPage / WaveformWidget
├─ VisionPage -> QTcpSocket bridge / overlay
└─ SystemLogPage
```

CLI：`--gateway`、`--wav-input`、`--wav-loop`、`--wav-speed`、`--python-bin`、`--vision-host`、`--vision-port`。vision port 0 禁用；fixed RF chain 缺 WAV 会明确失败。

## 删除/保留说明

未使用的 bridge client-name bookkeeping、vision normalization reason 已删除；路径、进程、socket、JSON、图片尺寸、protocol/parser 与 stop/timeout 检查仍保留，因为它们保护外部输入和生命周期。

## 测试映射

`rf_protocol_contract_test` 覆盖 C 协议恢复；root Python unittest 覆盖 `--json-include-all-clusters`；fake scripts 是手工验证工具，不是 CTest。详细命令见 [project2_pc_sim_reading_guide.md](project2_pc_sim_reading_guide.md)。
