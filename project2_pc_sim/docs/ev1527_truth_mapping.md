# EV1527 真值映射与字段边界

这份文档只解释 `project2_pc_sim` 当前 RF 链里的 EV1527 字段语义，不解释 `project2_master` 的板端真设备采集链。

## 1. 适用范围

这里讨论的是这一条链：

```text
WAV
-> wav_to_pulses.py
-> replay_pulse_timeline.py
-> AA55 binary stream
-> rf_gateway
-> Qt RF page
```

它不覆盖：

- `/dev/rf433` 真链
- 真实 UART/serdev 驱动
- Vision payload
- MQTT / RTSP 上报链

## 2. 当前 RF 真值面在哪里

在 `pc_sim` 里，RF 真值面不是单点，而是分层的：

### 第 1 层：WAV 与候选帧

来源：

- 原始 `WAV`
- `python/wav_to_pulses.py` 产出的 `pulse_runtime.json`

这里保存的是：

- `candidate_idx`
- `candidate_wav_sec`
- `pulse`

这层的意义是：

- 记录“哪些脉冲帧被拿去回放”
- 记录“这些帧在原始 WAV 中大概对应哪个时间点”

### 第 2 层：AA55 回放流

来源：

- `python/replay_pulse_timeline.py::_encode_frame()`

这里保存的是协议层真值：

- `0xAA 0x55`
- pulse 个数
- pulse 数组
- CRC

这层的意义是：

- 定义回放给 `rf_gateway` 的低层输入协议

### 第 3 层：`rf_gateway` 解码结果

来源：

- `linux_app/main.c::on_rf_frame()`

这里保存的是业务侧字段：

- `addr`
- `key`
- `conf`
- `src`
- `pulses`
- `seq`

这层的意义是：

- 定义 Qt RF 页当前真正消费的解码结果结构

### 第 4 层：Qt 派生展示字段

来源：

- `qt_gui/rf/rf_gateway_client.cpp`
- `qt_gui/core/rf_utils.cpp`

这里会产生两个重要派生值：

- `wav_sec`
- 合成 waveform

这层不是 RF 原始真值层，而是“为了 UI 更好读”的派生层。

## 3. WAV 候选帧字段映射

`python/wav_to_pulses.py` 写出的 `pulse_runtime.json` 中，单帧最重要的字段是：

| 字段 | 来源 | 含义 |
| --- | --- | --- |
| `candidate_idx` | `extract_frames()` 末尾按顺序补号 | 候选帧编号，从 1 开始 |
| `candidate_wav_sec` | `start_sample / sample_rate` | 该候选帧在原始 WAV 中的大致秒数 |
| `pulse` | run-length 窗口提取结果 | 一组 EV1527 候选脉冲宽度，单位微秒 |

这里要注意：

- `candidate_wav_sec` 不是硬件时间戳
- 它只是“WAV 内相对时间”
- 它后面会被 replay 用来恢复时间节奏

摘自 `python/wav_to_pulses.py`：

```python
return {
    "candidate_wav_sec": float(start_sample) / float(sample_rate),
    "pulse": pulse,
}
```

```python
for idx, row in enumerate(frames, start=1):
    row["candidate_idx"] = idx
```

这段代码把三个关键事实钉死了：`candidate_wav_sec` 直接来自 `start_sample / sample_rate`，`pulse` 是候选脉冲数组，`candidate_idx` 是在输出前按顺序补上的编号。也就是说，`pulse_runtime.json` 里的时间和序号都属于 `pc_sim` 自己的离线回放辅助元数据。

## 4. AA55 回放帧映射

`python/replay_pulse_timeline.py::_encode_frame()` 会把 `pulse` 编码成：

| 字段 | 编码方式 | 说明 |
| --- | --- | --- |
| `SYNC0` | `0xAA` | 固定 |
| `SYNC1` | `0x55` | 固定 |
| `LEN` | little-endian `uint16` | pulse 个数 |
| `PAYLOAD[i]` | little-endian `uint16` | 每个脉冲宽度，单位微秒 |
| `CRC` | 对 `LEN + PAYLOAD` 做 XOR | 协议校验 |

这意味着：

- `pc_sim` 的 replay 输出不是文本
- `rf_gateway` 吃的是协议化二进制帧
- 这条协议和 `common/rf_protocol.h` 里的定义保持一致

摘自 `python/replay_pulse_timeline.py`：

```python
def _encode_frame(pulse: list[int]) -> bytes:
    if not pulse:
        return b""
    norm = [int(v) for v in pulse if 0 < int(v) <= 65535]
    if not norm:
        return b""

    header = bytearray([0xAA, 0x55, len(norm) & 0xFF, (len(norm) >> 8) & 0xFF])
    payload = bytearray()
    for p in norm:
        payload.append(p & 0xFF)
        payload.append((p >> 8) & 0xFF)

    crc = _crc8(bytes(header[2:]) + bytes(payload))
    return bytes(header + payload + bytes([crc]))
```

这段就是 replay 编码的真实实现：固定 `0xAA 0x55` 头、`LEN` 小端、每个 pulse 一个小端 `uint16`，最后补 XOR CRC。文档里关于 AA55 协议层真值的描述直接来自这里。

同文件的时间线构造也明确依赖 `candidate_wav_sec`：

```python
start_sec = float(row.get("candidate_wav_sec", 0.0))
valid.append(
    {
        "idx": int(row.get("candidate_idx", len(valid) + 1)),
        "start_sec": max(0.0, start_sec),
        "pulse": pulse,
    }
)
```

```python
base = frames[0]["start_sec"]
timeline = []
for idx, row in enumerate(frames, start=1):
    rel_t = max(0.0, row["start_sec"] - base)
    pkt = _encode_frame(row["pulse"])
    if pkt:
        timeline.append(
            {
                "idx": int(row.get("idx", idx)),
                "wav_sec": float(row["start_sec"]),
                "rel_sec": rel_t,
                "packet": pkt,
            }
        )
```

这说明 replay 不只是“编码成帧”，还会把 `candidate_wav_sec` 先转成 `start_sec / rel_sec`，再按时间节奏把二进制包写到 `stdout`。

## 5. `rf_gateway` 输出字段映射

`linux_app/main.c::on_rf_frame()` 成功发布时的字段如下：

| 字段 | 当前来源 | 含义 | 备注 |
| --- | --- | --- | --- |
| `addr` | `rf_decode_frame()` | 24-bit raw code 的十六进制字符串 | 形如 `0x35A1BC` |
| `key` | `raw_code & 0x0F` | 低 4 bit 按键值 | 以字符串输出 |
| `conf` | C 解码器评分 | 解码置信度 | 浮点数 |
| `src` | `rf_decode.c` 填 `"c"` | 当前解码来源标签 | 不是 replay / master / driver 标签 |
| `pulses` | `frame->len` | 本次帧的脉冲个数 | 通常接近 EV1527 帧长 |
| `seq` | `ctx->frame_seq` | `rf_gateway` 看到的帧序号 | 单进程内递增 |

最容易误解的是 `src`：

- 在 `fake_event_record_test.py` 默认示例里，你可以传 `src="replay"`
- 但真实 `pc_sim/linux_app/main.c` 输出里，当前 `src` 来自 C 解码器，值是 `"c"`

因此：

- `fake_event_record_test.py` 里的 `src` 只是本地占位示例
- 不应把那个默认值当成 `rf_gateway` 当前真实输出值

摘自 `linux_app/main.c`：

```c
printf(
    "{\"addr\":\"%s\",\"key\":\"%s\",\"conf\":%.2f,\"src\":\"%s\",\"pulses\":%u,\"seq\":%u}\n",
    pkt.addr,
    pkt.key,
    pkt.confidence,
    pkt.source,
    frame->len,
    (unsigned)ctx->frame_seq
);
```

这段代码就是当前 gateway JSON 的真实来源。它说明 `pc_sim` 这条链里，Qt 真正接到的是 `addr` / `key` / `conf` / `src` / `pulses` / `seq` 这一组字段，而不是带 `wav_sec` 的原生底层协议。

## 6. `seq` 与 `wav_sec` 的关系

当前 replay 链没有 sideband 元数据协议，Qt 是通过预读 `pulse_runtime.json` 自己把时间补回去的。

流程是：

1. Qt 调 `wav_to_pulses.py` 生成 `pulse_runtime.json`
2. Qt 在 `onPrepFinished()` 里读出每个 `candidate_idx -> candidate_wav_sec`
3. `replay_pulse_timeline.py` 按同样顺序回放帧
4. `rf_gateway` 发布 JSON 时带 `seq`
5. Qt 在 `handleGatewayLine()` 里尝试用 `seq` 对应回 `candidate_wav_sec`
6. 如果对应成功，就把这个值写进 `event.candidateWavSec`

因此：

| 字段 | 真正来源 |
| --- | --- |
| `seq` | `rf_gateway` 内部处理序号 |
| `wav_sec` | Qt 依据 `pulse_runtime.json` 的索引关系回填 |

`wav_sec` 不是底层协议原生字段，也不是硬件时钟。

Qt 回填 `wav_sec` 的代码也很直接。摘自 `qt_gui/rf/rf_gateway_client.cpp`：

```cpp
const QJsonValue idxValue = frameObj.contains(QStringLiteral("candidate_idx"))
                                ? frameObj.value(QStringLiteral("candidate_idx"))
                                : frameObj.value(QStringLiteral("idx"));
const QJsonValue wavSecValue = frameObj.contains(QStringLiteral("candidate_wav_sec"))
                                   ? frameObj.value(QStringLiteral("candidate_wav_sec"))
                                   : frameObj.value(QStringLiteral("wav_sec"));
if (frameIndex > 0 && wavSec >= 0.0) {
    replayWavSecByIdx_.insert(frameIndex, wavSec);
}
```

这段发生在 `onPrepFinished()` 里，说明 Qt 会先把 `pulse_runtime.json` 预读成 `candidate_idx -> candidate_wav_sec` 的内存映射。

同文件的回填逻辑是：

```cpp
if (event.frameSeq > 0 && event.candidateWavSec < 0.0) {
    const int idx = static_cast<int>(event.frameSeq);
    if (replayWavSecByIdx_.contains(idx)) {
        event.candidateWavSec = replayWavSecByIdx_.value(idx, -1.0);
    }
}
if (event.candidateWavSec >= 0.0) {
    payloadObj.insert(QStringLiteral("wav_sec"), event.candidateWavSec);
}
```

也就是说，Qt 不是从 gateway 收到一个原生 `wav_sec` 字段，而是先看 JSON 里有没有，没有就拿 `seq -> replayWavSecByIdx_` 去补，最后才把 `wav_sec` 写进本地展示和 report payload。

## 7. Qt waveform 的真值边界

Qt 页面的 waveform 不是直接由 `pulse` 数组画出来的。当前流程是：

```text
addr string
-> parseRawCode()
-> buildWaveformFromRawCode()
-> fixed-t synthetic EV1527 waveform
```

映射参数写死在 `qt_gui/core/rf_utils.cpp`：

- 基础时间 `tUs = 300`
- sync 高电平 `4 * tUs`
- sync 低电平 `124 * tUs`
- bit `1` 映射为 `12T high + 4T low`
- bit `0` 映射为 `4T high + 12T low`

这表示：

- waveform 的用途是帮助人看懂“解码出了什么码”
- 它不是原始 `pulse` 的逐项可视化

如果你要核对真实回放脉冲，请看：

- `pulse_runtime.json`
- replay 输出协议
- `rf_gateway` 解码结果

不要只看 UI 波形。

摘自 `qt_gui/core/rf_utils.cpp`：

```cpp
uint32_t parseRawCode(const QString &text) {
    QString value = text.trimmed();
    if (value.startsWith("0x", Qt::CaseInsensitive)) {
        value = value.mid(2);
    }
    bool ok = false;
    const uint32_t code = value.toUInt(&ok, 16);
    if (!ok) {
        return 0;
    }
    return code & 0x00FFFFFFu;
}

QVector<int> buildWaveformFromRawCode(uint32_t rawCode) {
    QVector<int> pulses;
    pulses.reserve(2 + 24 * 2);

    const int tUs = 300;
    pulses.append(4 * tUs);
    pulses.append(124 * tUs);
```

```cpp
for (int bit = 23; bit >= 0; --bit) {
    const bool one = ((rawCode >> bit) & 0x1u) != 0u;
    if (one) {
        pulses.append(12 * tUs);
        pulses.append(4 * tUs);
    } else {
        pulses.append(4 * tUs);
        pulses.append(12 * tUs);
    }
}
```

这就是当前示意波形的真实来源：Qt 先把 `addr` 解析成 24-bit raw code，再按写死的 `tUs = 300` 和 EV1527 bit 映射合成一组脉冲宽度。它不是从 replay 原始 `pulse[]` 逐项画出来的“原始波形”。

## 8. 与 `project2_master` 的映射差异

在 `master` 里，RF 真值面来自：

- 真驱动
- 真 `/dev/rf433`
- 真 `struct rf433_frame`

在 `pc_sim` 里，RF 真值面来自：

- WAV 候选帧
- AA55 回放流
- `rf_gateway(stdin)` 解码

对比：

| 维度 | `project2_master` | `project2_pc_sim` |
| --- | --- | --- |
| 底层输入 | `/dev/rf433` | replay 二进制流 |
| 时间参考 | 驱动/运行时真实帧序列 | `candidate_wav_sec` + `seq` 回填 |
| 波形展示 | 可以围绕真实帧展开 | 当前页面是合成波形 |
| 角色 | 主验收真值面 | 离线模拟真值面 |

因此，这份映射文档的结论只能用于：

- 解释 `pc_sim` 当前 RF 字段合同
- 帮助离线验证使用者判断“Qt 接的是不是当前这套字段”

不能用于替代 `master` 的硬件验收结论。

## 9. 对 fake 脚本的解释

### `fake_detection_stream_test.py`

这个脚本和 EV1527 无关，不属于本文件讨论范围。

### `fake_event_record_test.py`

这个脚本只复用 RF 字段形状，不代表真实 RF 运行链。

它要求的最小 RF JSON 条件是：

| 字段 | 要求 |
| --- | --- |
| `addr` | 非空字符串 |
| `key` | 标量，可转成字符串 |
| `conf` | 数值 |
| `src` | 非空字符串 |
| `seq` | 可选数值 |
| `wav_sec` | 可选数值 |
| `pulses` | 可选数值 |

这只是“本地交接面验证合同”，不是 RF 硬件真值定义。

## 10. 最终应如何表述当前 EV1527 语义

最准确、最不容易误导的说法是：

**`project2_pc_sim` 当前通过 WAV 提取 EV1527 候选脉冲帧，再把这些脉冲按 AA55 协议回放给 `rf_gateway` 解码。Qt 页面消费的是解码后的 JSON 字段，并附带由 Qt 自己回填的 `wav_sec` 和由 `addr` 合成的 waveform。**

如果要再补一句边界，就补：

**这套语义用于离线验证和代码阅读，不等价于 `project2_master` 的 `/dev/rf433` 板端真链。**
