# project2 共享 RF 协议深读

这份文档只讲“硬件侧和 master 侧怎样约定一帧脉冲”。如果你想看 EV1527 解码、MQTT 或 Qt，请去后面的 driver/userland 文档。这里必须把边界收得很紧。

兼容说明：以下先补回 `HEAD` 版章节骨架，便于沿用旧目录、旧引用和旧阅读顺序；后文现有正文、源码摘录和细讲全部保留。

## 补强索引

兼容旧版目录：下文现有正文继续覆盖共享 ABI、字节流格式、`encode()`、hardware parser、Linux driver parser 边界和推荐走读顺序，本轮新增代码块与细讲保持不删。

### 0. 快速索引：先读哪三段

兼容旧版目录：对应下文现有 `## 1. 先把边界讲死`、`## 3. 共享数据模型：rf_frame_t`、`## 6. rf_proto_encode() 真正做了什么` 与 `## 11. 推荐的代码走读顺序`。

### 1. 术语与契约表

兼容旧版目录：对应当前正文里 `frame/pulse/payload/sync/len/crc/encode/parser` 的边界说明。

## 1. 阅读顺序

兼容旧版目录：对应下文现有 `## 11. 推荐的代码走读顺序`。

## 2. 文件职责

兼容旧版目录：对应下文现有 `## 2. 相关源文件`、`## 7. 为什么 master/common 不带 parser`、`## 8. hardware parser 与 Linux 驱动 parser 的关系`。

### 2.1 master/common 侧

兼容旧版目录：对应当前正文里 `project2_master/common/rf_protocol.h/.c` 的共享 ABI 和 `encode()` 责任。

### 2.2 hardware/Hardware 侧

兼容旧版目录：对应当前正文里 `project2_hardware/Hardware/RF_Protocol.h/.c` 的镜像编码与 parser 责任。

### 2.3 关系结论

兼容旧版目录：对应当前正文里“共享同一套字节契约，而不是共享同一个实现”的边界总结。

## 3. 先看 ABI

兼容旧版目录：对应下文现有 `## 1. 先把边界讲死`、`## 3. 共享数据模型：rf_frame_t`、`## 4. 线上的帧格式`、`## 5. CRC 规则为什么很重要`。

### 3.1 头文件前置条件

兼容旧版目录：对应当前正文里共享头文件、类型、跨端约束与最小公开面说明。

### 3.2 共享常量

兼容旧版目录：对应当前正文里同步字、缓冲区上限和协议字段规则说明。

#### `RF_PROTO_SYNC0` / `RF_PROTO_SYNC1`

兼容旧版目录：对应下文现有 `## 4. 线上的帧格式` 中的同步头说明。

#### `RF_BUFFER_SIZE`

兼容旧版目录：对应下文现有 `## 3. 共享数据模型：rf_frame_t` 与长度上限说明。

### 3.3 `rf_frame_t`

兼容旧版目录：对应下文现有 `## 3. 共享数据模型：rf_frame_t`。

### 2. 资源生命周期与所有权

兼容旧版目录：对应当前正文里 `rf_frame_t`、payload、parser 输出和两端责任边界的说明。

### 3.4 hardware 侧额外 ABI

兼容旧版目录：对应下文现有 `## 8. hardware parser 与 Linux 驱动 parser 的关系`。

#### `rf_parse_state_t`

兼容旧版目录：对应当前正文里 parser 状态机阶段说明。

#### `rf_proto_parser_t`

兼容旧版目录：对应当前正文里 parser 上下文、payload 缓冲和 CRC 累积说明。

## 4. master/common/rf_protocol.c

兼容旧版目录：对应下文现有 `## 5. CRC 规则为什么很重要` 与 `## 6. rf_proto_encode() 真正做了什么`。

### 4.1 `rf_proto_crc8()`

兼容旧版目录：对应下文现有 `## 5. CRC 规则为什么很重要`。

### 4.2 `rf_proto_encode()`

兼容旧版目录：对应下文现有 `## 6. rf_proto_encode() 真正做了什么`。

#### 1. 空指针和长度校验

兼容旧版目录：对应当前正文里输入校验、长度上限和失败返回说明。

#### 2. 计算总长度

兼容旧版目录：对应当前正文里 `SYNC + LEN + PAYLOAD + CRC` 的总长度公式。

#### 3. 写同步字与长度

兼容旧版目录：对应下文现有 `## 4. 线上的帧格式` 与 `## 6. rf_proto_encode() 真正做了什么`。

#### 4. 写 payload

兼容旧版目录：对应当前正文里按小端写出脉冲数组的说明。

#### 5. 计算 CRC

兼容旧版目录：对应下文现有 `## 5. CRC 规则为什么很重要`。

#### 6. 写 CRC 并返回

兼容旧版目录：对应当前正文里尾部校验字节和返回长度说明。

## 5. hardware/Hardware/RF_Protocol.c

兼容旧版目录：对应下文现有 `## 8. hardware parser 与 Linux 驱动 parser 的关系`。

### 5.1 `rf_proto_parser_init()`

兼容旧版目录：对应当前正文里 parser 初始化、复位和重新同步说明。

### 5.2 `rf_proto_parser_consume()`

兼容旧版目录：对应当前正文里逐字节消费、校验和出帧逻辑。

#### `RF_PARSE_SYNC0`

兼容旧版目录：对应当前正文里等待第一个同步字节的说明。

#### `RF_PARSE_SYNC1`

兼容旧版目录：对应当前正文里第二个同步字节、保留重同步机会的说明。

#### `RF_PARSE_LEN0`

兼容旧版目录：对应当前正文里长度低字节与 CRC 初值说明。

#### `RF_PARSE_LEN1`

兼容旧版目录：对应当前正文里长度高字节、上限校验与失败复位说明。

#### `RF_PARSE_PAYLOAD`

兼容旧版目录：对应当前正文里 payload 累积与 CRC 递推说明。

#### `RF_PARSE_CRC`

兼容旧版目录：对应当前正文里尾部校验通过后重组 `rf_frame_t` 的说明。

### 3. 解析边界条件

兼容旧版目录：对应当前正文里同步丢失、长度非法、CRC 错误、恢复路径和上层可见性的说明。

## 6. master/hardware 对齐关系

兼容旧版目录：对应下文现有 `## 7. 为什么 master/common 不带 parser`、`## 8. hardware parser 与 Linux 驱动 parser 的关系`、`## 10. 这条边界对后续文档有什么影响`。

### 6.1 先看对齐表

兼容旧版目录：对应当前正文里同步字、长度、payload、CRC 和 parser 责任对照。

### 6.2 真正对齐的是什么

兼容旧版目录：对应当前正文里“共享的是字节契约，不是上层解码结果”的说明。

### 6.3 为什么 hardware 多出 parser

兼容旧版目录：对应下文现有 `## 7. 为什么 master/common 不带 parser` 与 `## 8. hardware parser 与 Linux 驱动 parser 的关系`。

### 6.4 对齐方式不是共享实现，而是共享契约

兼容旧版目录：对应下文现有 `## 10. 这条边界对后续文档有什么影响`。

## 7. 数据流总图

兼容旧版目录：对应当前正文里编码链、parser 链和 master 侧消费链的串联说明。

### 7.1 hardware 侧主链路

兼容旧版目录：对应当前正文里 `rf_frame_t -> encode -> UART` 的发送链。

### 7.2 hardware 侧镜像链路

兼容旧版目录：对应当前正文里 `UART bytes -> parser -> rf_frame_t` 的恢复链。

### 7.3 master 侧消费链路

兼容旧版目录：对应当前正文里 Linux driver / userland / Qt 继续消费共享脉冲帧的说明。

## 8. 逐项对照结论

兼容旧版目录：对应下文现有 `## 9. 共享协议不包含什么` 与 `## 10. 这条边界对后续文档有什么影响`。

### 8.1 这份协议的边界

兼容旧版目录：对应下文现有 `## 1. 先把边界讲死`。

### 8.2 最敏感的几个约束

兼容旧版目录：对应当前正文里同步字、长度、小端 payload、CRC 覆盖范围和 parser 语义说明。

### 8.3 你要抓住的最终结论

兼容旧版目录：对应下文现有 `## 10. 这条边界对后续文档有什么影响` 与 `## 11. 推荐的代码走读顺序` 的收束。

## 9. 最后一眼只看代码

兼容旧版目录：对应当前正文里 `rf_frame_t`、线上的帧格式、`rf_proto_encode()` 和 parser 状态机的代码摘录。

### 4. 最后一眼只看代码

兼容旧版目录：对应当前正文里保留的真实源码片段与最小阅读骨架。

## 1. 先把边界讲死

共享协议只到 pulse frame 为止。

它定义的是：

- 帧同步头是什么
- 长度字段怎么编码
- 每个脉冲宽度怎么落到字节流
- CRC 怎么算

它不定义：

- `addr`
- `key`
- `conf` / `confidence`
- `source`
- MQTT topic
- Qt 页面展示字段

因此，任何 `addr/key/conf` 都只能写成“上层解码结果”，不能写成“共享协议字段”。

## 2. 相关源文件

master 侧：

- `project2_master/common/rf_protocol.h`
- `project2_master/common/rf_protocol.c`

hardware 侧：

- `project2_hardware/Hardware/RF_Protocol.h`
- `project2_hardware/Hardware/RF_Protocol.c`

观察这四个文件时，你会看到一个很清楚的分工：

- master 公共头里只保留最小 ABI 和编码函数
- hardware 侧除了同样的编码函数，还提供了字节流 parser
- Linux 驱动没有直接复用 hardware 的 parser 源码，而是按同一套规则实现了自己的状态机

## 3. 共享数据模型：`rf_frame_t`

两个世界共同认识的数据结构是：

```c
typedef struct {
    uint16_t pulse[RF_BUFFER_SIZE];
    uint16_t len;
} rf_frame_t;
```

读这个结构时要抓住三个事实：

1. 它只是一组脉冲宽度，单位是微秒级整型值。
2. 它没有时间戳、没有序号、没有解码语义。
3. `len` 只表示数组里前多少个 `pulse[]` 元素有效。

也就是说，共享 ABI 的最小语义是：

“这里有一帧高低电平交替脉冲，它包含 `len` 个脉冲宽度值。”

来源：`project2_master/common/rf_protocol.h`，结构：`rf_frame_t`，作用：定义 master/common 暴露给上下游的最小共享脉冲帧模型。

```c
#define RF_PROTO_SYNC0 0xAAu
#define RF_PROTO_SYNC1 0x55u
#define RF_BUFFER_SIZE 1024u

typedef struct {
    uint16_t pulse[RF_BUFFER_SIZE];
    uint16_t len;
} rf_frame_t;

uint8_t rf_proto_crc8(const uint8_t *data, size_t len);
size_t rf_proto_encode(const rf_frame_t *frame, uint8_t *out, size_t out_capacity);
```

这段头文件非常重要，因为它把 shared 层边界收得很死：

- 共享层只公开 `pulse[] + len`。
- 共享层只公开“编码函数”，没有公开 parser、时间戳、序号或 decode 结果。
- 这意味着 `addr/key/conf`、`timestamp_ns/seq` 这些字段天然都不属于 shared 层。

## 4. 线上的帧格式

当前代码约定的字节流格式可以直接写成下面这张表：

| 字段 | 字节数 | 含义 |
| --- | --- | --- |
| `SYNC0` | 1 | 固定 `0xAA` |
| `SYNC1` | 1 | 固定 `0x55` |
| `LEN_LO` | 1 | 脉冲数低字节 |
| `LEN_HI` | 1 | 脉冲数高字节 |
| `PAYLOAD` | `len * 2` | 每个脉冲宽度按 `uint16_t` 小端写入 |
| `CRC` | 1 | 对 `LEN + PAYLOAD` 做逐字节 XOR |

写成公式就是：

```text
AA 55 LEN_LO LEN_HI PULSE0_LO PULSE0_HI ... PULSEN_LO PULSEN_HI CRC
```

这里没有任何压缩语义，也没有 EV1527 专属字段。它只是把一帧脉冲宽度数组稳定地搬过 UART。

## 5. CRC 规则为什么很重要

`rf_proto_crc8()` 的实现非常直接：对 `LEN` 和 `PAYLOAD` 的每个字节依次 XOR。

它的价值不是“强校验”，而是“廉价地挡掉显然坏帧”，这样：

- hardware 侧 parser 可以在收包时发现错误
- Linux 驱动也可以在内核里做同样的判断
- userland 不需要再去面对碎片化字节流

对这份代码走读而言，更重要的是一致性：master/common、hardware 侧都用同一条 XOR 规则，所以“帧到底合法吗”这个判断不会在两端漂移。

来源：`project2_master/common/rf_protocol.c`，函数：`rf_proto_crc8()`，作用：对 `LEN + PAYLOAD` 做逐字节 XOR，得到帧尾校验字节。

```c
uint8_t rf_proto_crc8(const uint8_t *data, size_t len) {
    size_t i = 0u;
    uint8_t crc = 0u;
    for (i = 0u; i < len; ++i) {
        crc ^= data[i];
    }
    return crc;
}
```

这里没有多项式、查表或分段权重，只有一条非常朴素的 XOR 规则。所以这层的目标确实只是“挡掉显然坏帧”，不是提供强纠错能力。

## 6. `rf_proto_encode()` 真正做了什么

master/common 里的 `rf_proto_encode()` 只负责一件事：把 `rf_frame_t` 序列化成线上的字节流。

它依次完成：

1. 校验输入指针和 `frame->len`
2. 计算 payload 字节数 `len * 2`
3. 写入 `AA 55`
4. 以小端写入 `LEN`
5. 逐个写入 `pulse[i]` 的低字节和高字节
6. 对 `LEN + PAYLOAD` 计算 XOR CRC
7. 把 CRC 写到帧尾

如果你在阅读时只想抓主干，可以把它理解成：

`rf_frame_t` -> “稳定、可在 UART 上传输的一行二进制帧”

来源：`project2_master/common/rf_protocol.c`，函数：`rf_proto_encode()`，作用：把 `rf_frame_t` 按 `AA55 + LEN + PAYLOAD + CRC` 线协议写成字节流。

```c
size_t rf_proto_encode(const rf_frame_t *frame, uint8_t *out, size_t out_capacity) {
    uint16_t i = 0u;
    uint16_t bytes = 0u;
    uint8_t crc = 0u;
    size_t total = 0u;

    if (frame == NULL || out == NULL) {
        return 0u;
    }
    if (frame->len == 0u || frame->len > RF_BUFFER_SIZE) {
        return 0u;
    }

    bytes = (uint16_t)(frame->len * 2u);
    total = (size_t)2u + 2u + bytes + 1u;
    if (out_capacity < total) {
        return 0u;
    }

    out[0] = RF_PROTO_SYNC0;
    out[1] = RF_PROTO_SYNC1;
    out[2] = (uint8_t)(frame->len & 0xFFu);
    out[3] = (uint8_t)((frame->len >> 8u) & 0xFFu);

    for (i = 0u; i < frame->len; ++i) {
        const uint16_t p = frame->pulse[i];
        const size_t off = (size_t)4u + (size_t)i * 2u;
        out[off] = (uint8_t)(p & 0xFFu);
        out[off + 1u] = (uint8_t)((p >> 8u) & 0xFFu);
    }

    crc = rf_proto_crc8(&out[2], (size_t)2u + bytes);
    out[4u + bytes] = crc;
    return total;
}
```

这段实现和 `rf_frame_t` 一起构成了 shared 层的完整“正向合同”：

- `SYNC0/SYNC1` 固定为 `0xAA/0x55`。
- `LEN` 是小端 `uint16_t`，不是文本或变长编码。
- `PAYLOAD` 只是 `uint16_t` 脉冲宽度数组原样落字节，没有压缩、没有解码语义。

## 7. 为什么 master/common 不带 parser

`project2_master/common/rf_protocol.h` 当前只暴露：

- `rf_frame_t`
- `rf_proto_crc8()`
- `rf_proto_encode()`

它没有暴露 parser API。和它对照，hardware 侧的 `RF_Protocol.h` 额外定义了：

- `rf_parse_state_t`
- `rf_proto_parser_t`
- `rf_proto_parser_init()`
- `rf_proto_parser_consume()`

这说明 master/common 的定位是“最小共享 ABI”，不是“提供一份跨所有环境直接复用的 parser 实现”。

来源：`project2_hardware/Hardware/RF_Protocol.h`，结构：`rf_parse_state_t` / `rf_proto_parser_t`，作用：证明 parser 能力目前只在 hardware 侧头文件暴露，不在 master/common 的共享头里暴露。

```c
typedef enum {
    RF_PARSE_SYNC0 = 0,
    RF_PARSE_SYNC1,
    RF_PARSE_LEN0,
    RF_PARSE_LEN1,
    RF_PARSE_PAYLOAD,
    RF_PARSE_CRC
} rf_parse_state_t;

typedef struct {
    rf_parse_state_t state;
    uint16_t expected_pulses;
    uint16_t payload_index;
    uint8_t payload[RF_BUFFER_SIZE * 2u];
    uint8_t crc;
} rf_proto_parser_t;

uint8_t rf_proto_crc8(const uint8_t *data, size_t len);
size_t rf_proto_encode(const rf_frame_t *frame, uint8_t *out, size_t out_capacity);
void rf_proto_parser_init(rf_proto_parser_t *parser);
int rf_proto_parser_consume(rf_proto_parser_t *parser, uint8_t byte, rf_frame_t *out_frame);
```

也就是说，shared 层真正跨目录共享的是 `rf_frame_t + crc8 + encode`；parser 只是某些运行环境各自需要的“本地实现能力”。

从架构上看，这是合理的：

- STM32 firmware 需要字节流 parser，因为它直接面对串口接收
- Linux 驱动也需要 parser，但它在内核环境里工作，要自己维护锁、队列、统计和在线状态
- 用户态 `rf_gateway` 已经不看原始字节流，只看 `/dev/rf433` 输出的整帧，所以根本不需要这个 parser

## 8. hardware parser 与 Linux 驱动 parser 的关系

hardware 侧 `rf_proto_parser_consume()` 和 Linux 驱动里的 `parser_feed_byte()` 虽然不共用源码，但状态机是同构的：

```text
SYNC0 -> SYNC1 -> LEN0 -> LEN1 -> PAYLOAD -> CRC
```

它们共同遵守的规则包括：

- 只有看到 `0xAA 0x55` 才进入长度解析
- `LEN` 是小端 `uint16_t`
- `LEN == 0` 或超过上限都视为错误
- payload 按脉冲宽度小端读取
- CRC 不匹配直接丢弃当前帧并复位

你在读 driver 文档时可以把这一点当成“协议一致性检查清单”：即使实现位置不同，语义也必须保持一致。

来源：`project2_hardware/Hardware/RF_Protocol.c`，函数：`rf_proto_parser_consume()`，作用：hardware 侧按字节恢复 `rf_frame_t`。

```c
int rf_proto_parser_consume(rf_proto_parser_t *parser, uint8_t byte, rf_frame_t *out_frame) {
    if (parser == NULL || out_frame == NULL) {
        return -1;
    }

    switch (parser->state) {
        case RF_PARSE_SYNC0:
            if (byte == RF_PROTO_SYNC0) {
                parser->state = RF_PARSE_SYNC1;
            }
            break;
        case RF_PARSE_SYNC1:
            if (byte == RF_PROTO_SYNC1) {
                parser->state = RF_PARSE_LEN0;
                parser->crc = 0u;
                parser->expected_pulses = 0u;
                parser->payload_index = 0u;
            } else if (byte != RF_PROTO_SYNC0) {
                parser->state = RF_PARSE_SYNC0;
            }
            break;
        case RF_PARSE_LEN0:
            parser->expected_pulses = byte;
            parser->crc = byte;
            parser->state = RF_PARSE_LEN1;
            break;
        case RF_PARSE_LEN1:
            parser->expected_pulses |= (uint16_t)((uint16_t)byte << 8u);
            parser->crc ^= byte;
            if (parser->expected_pulses == 0u || parser->expected_pulses > RF_BUFFER_SIZE) {
                rf_proto_parser_init(parser);
                return -1;
            }
            parser->state = RF_PARSE_PAYLOAD;
            break;
        case RF_PARSE_PAYLOAD:
            parser->payload[parser->payload_index++] = byte;
            parser->crc ^= byte;
            if (parser->payload_index >= (uint16_t)(parser->expected_pulses * 2u)) {
                parser->state = RF_PARSE_CRC;
            }
            break;
        case RF_PARSE_CRC: {
            uint16_t i = 0u;
            if (byte != parser->crc) {
                rf_proto_parser_init(parser);
                return -1;
            }
            out_frame->len = parser->expected_pulses;
            for (i = 0u; i < parser->expected_pulses; ++i) {
                const uint16_t lo = parser->payload[(size_t)i * 2u];
                const uint16_t hi = parser->payload[(size_t)i * 2u + 1u];
                out_frame->pulse[i] = (uint16_t)(lo | (uint16_t)(hi << 8u));
            }
            rf_proto_parser_init(parser);
            return 1;
        }
        default:
            rf_proto_parser_init(parser);
            return -1;
    }

    return 0;
}
```

来源：`project2_master/linux_driver/rf433_drv.c`，函数：`parser_feed_byte()`，作用：Linux driver 侧按同一协议状态机恢复一帧，并在 CRC 成功后转给 `parser_emit_frame()`。

```c
static void parser_feed_byte(struct rf433_priv *priv, u8 byte)
{
	priv->last_byte_jiffies = jiffies;

	switch (priv->state) {
	case RF_ST_SYNC0:
		if (byte == SYNC0)
			priv->state = RF_ST_SYNC1;
		break;

	case RF_ST_SYNC1:
		if (byte == SYNC1) {
			priv->state       = RF_ST_LEN0;
			priv->crc_accum   = 0;
			priv->expected_pulses = 0;
			priv->payload_idx = 0;
		} else if (byte == SYNC0) {
			/* stay in SYNC1 — consecutive 0xAA */
		} else {
			priv->state = RF_ST_SYNC0;
		}
		break;

	case RF_ST_LEN0:
		priv->expected_pulses = byte;
		priv->crc_accum       = byte;
		priv->state           = RF_ST_LEN1;
		break;

	case RF_ST_LEN1:
		priv->expected_pulses |= (u16)byte << 8;
		priv->crc_accum       ^= byte;
		if (priv->expected_pulses == 0 ||
		    priv->expected_pulses > RF433_MAX_PULSES) {
			priv->stats.len_err++;
			parser_reset(priv);
		} else {
			priv->state = RF_ST_PAYLOAD;
		}
		break;

	case RF_ST_PAYLOAD:
		priv->payload_buf[priv->payload_idx++] = byte;
		priv->crc_accum ^= byte;
		if (priv->payload_idx >= (u16)(priv->expected_pulses * 2u))
			priv->state = RF_ST_CRC;
		break;

	case RF_ST_CRC:
		if (byte != priv->crc_accum) {
			priv->stats.crc_err++;
			parser_reset(priv);
		} else {
			parser_emit_frame(priv);
			parser_reset(priv);
		}
		break;
	}
}
```

两段代码对照起来，shared 层的真实边界就非常清楚了：

- 两边都遵守同一个 `AA55 -> LEN -> PAYLOAD -> CRC` 规则，这是共享协议。
- hardware 侧成功后写出 `rf_frame_t`，driver 侧成功后先写出 `struct rf433_frame`，但其中真正跨环境稳定的核心仍然只是 `pulse[] + len` 这层语义。
- `timestamp_ns`、`seq`、`online`、`addr`、`key`、`stdout JSON envelope` 都发生在 shared 层之外。

## 9. 共享协议不包含什么

这是最值得反复强调的一节。

### 9.1 不包含解码结果

`addr`、`key`、`confidence` 出现在 `linux_app/main.c` 组装的 `rf_event.payload` 里，来源是：

- `rf_decode_frame()`
- `rf_decode_ev1527_c_with_stats()`

它们是用户态把 pulse frame 进一步解释后的结果，不属于共享协议。

### 9.2 不包含驱动扩展元数据

用户态最终从驱动读到的是 `struct rf433_frame`，它比 `rf_frame_t` 多了：

- `timestamp_ns`
- `seq`

这些字段是 Linux 驱动为了用户态观察和统计追加的本地 ABI，不是 UART 共享协议的一部分。

### 9.3 不包含进程间 JSON 契约

Qt 看到的 `stdout JSON envelope` 也是上层契约。它发生在：

`/dev/rf433 -> rf_gateway -> JSON`

而不是：

`shared protocol -> JSON`

## 10. 这条边界对后续文档有什么影响

一旦你把共享协议边界收在 pulse frame，上层文档就会更清晰：

- driver 文档只解释“怎样把字节流变成 `/dev/rf433` 帧接口”
- userland 文档只解释“怎样把驱动帧变成解码结果和 JSON”
- Qt 文档只解释“怎样消费 JSON”

这能避免一个常见误读：把 `addr/key/conf` 误写成从硬件一路原生携带到 Qt 的字段。当前代码事实并不是这样。

## 11. 推荐的代码走读顺序

如果你想靠源码验证本文结论，推荐这样读：

1. `project2_master/common/rf_protocol.h`
   先看共享结构体和常量。
2. `project2_master/common/rf_protocol.c`
   再看编码函数如何落字节。
3. `project2_hardware/Hardware/RF_Protocol.h`
   看 parser 状态和上下文结构。
4. `project2_hardware/Hardware/RF_Protocol.c`
   看字节流如何还原成 `rf_frame_t`。
5. `project2_master/linux_driver/rf433_drv.c`
   对照 Linux 驱动里同构的 parser 状态机。

读完这五步，再去看 driver 和 userland 文档，边界就不会乱。
