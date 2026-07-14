# EV1527 字段来源与真值边界

本文防止把 WAV 候选、UART frame、driver frame、decoder event 和 Qt 状态中的同名字段混为一谈。

## 分层映射

| 层 | 事实源 | 主要字段 | 不应声称 |
|---|---|---|---|
| WAV | 音频采样与边沿 | sample rate、candidate wav sec、pulse list | 已通过 EV1527 解码 |
| `wav_to_pulses.py` | MCU-like stage-1 过滤 | `candidate_idx/candidate_wav_sec/pulse` | confidence、repeat-valid |
| AA55 replay | `replay_pulse_timeline.py` | LE16 count/pulses、XOR | driver timestamp/seq |
| PC gateway | C decoder + stability | `addr/key/conf/src/pulses/seq` | MQTT/真实 hardware driver |
| root `ev1527_decode.py` | offline 全 WAV 聚类 | cluster、confidence、occurrence、`repeat_valid` | 与 gateway seq 相同 |
| master driver | kernel receive time | `timestamp_ns/drv_seq/status/stats` | WAV 时间位置 |
| Qt | 所消费 backend 快照 | UI 格式化字段 | 新增未在输入出现的硬件事实 |

## 三种“时间/序号”

- `candidate_wav_sec`：候选在 WAV 中的时间位置。
- PC gateway `seq`：gateway 处理 frame 的进程内序号。
- master `drv_seq/timestamp_ns`：真实 kernel driver 接受合法 UART frame 时生成。

PC replay 不应伪造 driver timestamp 或 MQTT topic。

## pulse、code 与 key

EV1527 是 24-bit code，decoder 当前把完整 code 格式化为 `addr=0x%06X`，低 4 bit 格式化为十进制 `key`。PC gateway 输出 `pulses` 是 pulse 数，不是数组；master `rf_event` 则输出 `pulse_count` 和完整 `pulse_us[]`。

一个标准候选为同步高/低加 24 bit 的高/低，共约 50 pulse。AA55 协议合法不代表 EV1527 合法；decoder confidence/stability 属于后续语义层。

## checksum 术语

AA55 末字节是从 LE16 长度起逐字节 XOR。`_crc8`、`rf_proto_crc8`、`crc_err` 是 legacy 名，不是多项式 CRC。

## offline JSON 选择

根目录 `ev1527_decode.py --json-out` 默认写 repeat-aware 过滤后的 cluster。加 `--json-include-all-clusters` 后写所有 cluster，并保留每项 `repeat_valid`，便于比较被默认过滤的候选。该开关已实现，不是占位参数。

```bash
python -B ev1527_decode.py --wav capture03.wav --start-sec 0 --end-sec 10 \
  --json-out build/default.json
python -B ev1527_decode.py --wav capture03.wav --start-sec 0 --end-sec 10 \
  --json-out build/all.json --json-include-all-clusters
```

## 验证原则

写消费者时标注字段来源；比较两条链时使用 code/pulse 内容和 WAV 时间关联，不直接比较不同层的 seq。fake scripts 只验证 schema/UI，不是模型精度、硬件时序或 driver 证据。
