# project2 共享 RF 协议深读

这份文档只看两组真实源码：

- [project2_master/common/rf_protocol.h](D:/project/repos/project2/project2_master/common/rf_protocol.h)
- [project2_master/common/rf_protocol.c](D:/project/repos/project2/project2_master/common/rf_protocol.c)
- [project2_hardware/Hardware/RF_Protocol.h](D:/project/repos/project2/project2_hardware/Hardware/RF_Protocol.h)
- [project2_hardware/Hardware/RF_Protocol.c](D:/project/repos/project2/project2_hardware/Hardware/RF_Protocol.c)

先把边界说清楚：

- 这不是 EV1527 解码文档。
- 这不是 `RF_Capture.c` 的采集细节文档。
- 这不是 `RF_Uart.c` 的串口 FIFO 文档。
- 这是一份“协议 ABI + 编解码实现 + master/hardware 对齐关系”的深读。

你要抓住的点是：

1. `rf_frame_t` 是两端共享的数据模型。
2. `rf_proto_encode()` 只负责把 `rf_frame_t` 变成字节流。
3. `hardware/Hardware/RF_Protocol.c` 额外实现了一个流式 parser，但 live transmit 链路的核心仍然是 `encode`。
4. master/hardware 不是“共用同一个头文件路径”，而是“共用同一套字节契约”。

## 补强索引

### 0. 快速索引：先读哪三段

这段的作用是先把阅读路线压缩成最短路径。你不要一上来就啃完整个文件，先抓住三段就够了。

| 首读路线 | 先看什么 | 要解决的问题 |
|---|---|---|
| master 侧首读 | `project2_master/common/rf_protocol.h` -> `project2_master/common/rf_protocol.c` | 先确认 `rf_frame_t`、`SYNC`、`LEN`、`CRC` 的 ABI 和编码规则，搞清楚“这帧到底长什么样” |
| hardware 侧首读 | `project2_hardware/Hardware/RF_Protocol.h` -> `project2_hardware/Hardware/RF_Protocol.c` | 先确认 parser 状态机和 encode 是否对齐，搞清楚“这帧怎么从字节流里被捞回来” |
| 对照首读 | `project2_master/common/rf_protocol.c` -> `project2_hardware/Hardware/RF_Protocol.c` | 先对照 encode 和 parser，搞清楚“同一套契约在两端是不是一字不差” |

如果你只想用三段先抓主线，就按这个顺序：

1. 先看 `common/rf_protocol.h`，解决“协议边界和结构体长什么样”。
2. 再看 `common/rf_protocol.c`，解决“每个字段怎么落到字节上”。
3. 最后看 `Hardware/RF_Protocol.c`，解决“流式输入怎么同步、怎么恢复、怎么出帧”。

### 1. 术语与契约表

这段的作用是把同一批词先统一口径。你要抓住的点是：`frame`、`pulse`、`payload` 不是随便混着叫的，它们各自指的是不同层。

| 术语 | 这段的作用是 | 你要抓住的点是 | master 负责 | hardware 负责 |
|---|---|---|---|---|
| `frame` | 描述一帧完整业务数据 | 它等价于 `rf_frame_t`，不是串口字节流本身 | 生产/消费 `rf_frame_t` | 接收/还原 `rf_frame_t` |
| `pulse` | 描述单个脉冲样本 | 它是 `uint16_t` 宽度值，不是 1 字节 payload | 按脉冲数组组织数据 | 按脉冲数组恢复数据 |
| `payload` | 描述线上传输的原始字节段 | 它是 `LEN` 后面的连续字节，不是业务字段名 | `encode()` 写出 payload | `parser` 先缓存 payload，再重组 |
| `sync` | 描述帧同步边界 | 它是 `0xAA 0x55`，不是可变头 | 固定写入 | 固定识别 |
| `len` | 描述本帧脉冲数量 | 它不是字节数，而是 `pulse[]` 的元素数 | 写入脉冲数 | 读取脉冲数并校验上限 |
| `crc` | 描述完整性校验 | 它只覆盖 `LEN + PAYLOAD`，不覆盖 `SYNC` | 计算并写尾部 | 逐字节累计并比对 |
| `encode` | 描述结构化到字节流的转换 | 它是 master/common 的输出口 | `rf_proto_encode()` | 镜像一致的编码规则 |
| `parser` | 描述字节流到结构体的恢复 | 它是 hardware 侧的流式补偿能力 | 不负责状态机 | `rf_proto_parser_*()` |

你要抓住的边界是：

- `master/common` 负责定义和输出契约。
- `hardware/Hardware` 负责按同一契约接收、验证、恢复。
- 两端共享的是字节规则，不是实现细节。
- 不是“谁都能改一点再试”，而是“改一处就要同步对齐整条契约”。

---

## 1. 阅读顺序

建议按下面顺序读，不要反着跳：

1. 先看 [project2_master/common/rf_protocol.h](D:/project/repos/project2/project2_master/common/rf_protocol.h)
2. 再看 [project2_master/common/rf_protocol.c](D:/project/repos/project2/project2_master/common/rf_protocol.c)
3. 对照看 [project2_hardware/Hardware/RF_Protocol.h](D:/project/repos/project2/project2_hardware/Hardware/RF_Protocol.h)
4. 最后看 [project2_hardware/Hardware/RF_Protocol.c](D:/project/repos/project2/project2_hardware/Hardware/RF_Protocol.c)

为什么这么排：

- 先看头文件，先确认 ABI。
- 再看实现，确认每个字段怎么落字节。
- 再看 hardware 侧的镜像实现，确认哪里是完全对齐，哪里是扩展。
- 先看接口，再看数据流，不要上来就盯循环体。

如果你只想快速抓主线，直接记这一句：

> 这段协议的作用，是把“脉冲宽度数组”变成“可在 UART 上传输的稳定字节流”，并且能在另一端按同样规则还原回来。

---

## 2. 文件职责

### 2.1 master/common 侧

| 文件 | 职责 | 你要抓住的点 |
|---|---|---|
| `common/rf_protocol.h` | 定义共享 ABI、常量、结构体、公开函数声明 | 这是协议边界，不是业务逻辑 |
| `common/rf_protocol.c` | 实现 XOR 校验和封包 | 这里只有“编码”，没有“流式解析” |

### 2.2 hardware/Hardware 侧

| 文件 | 职责 | 你要抓住的点 |
|---|---|---|
| `Hardware/RF_Protocol.h` | 在共享 ABI 的基础上追加 parser ABI | 这是一个更完整的协议镜像 |
| `Hardware/RF_Protocol.c` | 实现同样的编码，并额外实现字节流解析状态机 | 编码和 master 对齐，parser 是 hardware 扩展 |

### 2.3 关系结论

不是“master 一套协议，hardware 另一套协议”。

而是：

- `master/common` 定义最小共享协议模型
- `hardware/Hardware` 在同一模型上补上反向解析能力

这就是为什么两个目录里都有 `RF_PROTO_SYNC0`、`RF_PROTO_SYNC1`、`RF_BUFFER_SIZE`、`rf_frame_t`，并且 `encode` 的实现逐字节对齐。

---

## 3. 先看 ABI

### 3.1 头文件前置条件

先看这段：

```c
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

...

#ifdef __cplusplus
}
#endif
```

这段的作用是：

- `stddef.h` 提供 `size_t`
- `stdint.h` 提供 `uint8_t`、`uint16_t`
- `extern "C"` 保证 C++ 侧链接名不被改写

依赖：

- 标准 C 头文件
- C/C++ 混合编译环境

输入：

- 编译器预处理阶段的头文件包含

输出：

- 对外稳定的 C ABI

去向：

- master 侧和 hardware 侧都可以按同一套函数名/结构体名调用

为什么这样设计：

- 协议层本来就是“跨编译单元、跨工程、甚至跨语言”的边界
- 这里必须先把 ABI 固定住，再谈实现

你要抓住的点是：

- 这不是“某个模块的私有辅助头”
- 而是“协议契约头”

### 3.2 共享常量

先看常量：

```c
#define RF_PROTO_SYNC0 0xAAu
#define RF_PROTO_SYNC1 0x55u
#define RF_BUFFER_SIZE 1024u
```

#### `RF_PROTO_SYNC0` / `RF_PROTO_SYNC1`

作用：

- 定义帧头同步字节

依赖：

- 流式接收端需要一个可重锁定的边界标记

输入：

- 编码端固定写入
- 解析端逐字节匹配

输出：

- `0xAA 0x55` 这一段帧头

去向：

- `rf_proto_encode()` 写入输出缓冲区
- `rf_proto_parser_consume()` 用它做状态机起点

为什么这样设计：

- 这是一种低成本的帧边界标记
- 流里一旦丢字节，parser 还能靠同步字重新找回帧边界

不是 X，而是 Y：

- 不是为了“好看”
- 而是为了“在字节流里快速重同步”

#### `RF_BUFFER_SIZE`

作用：

- 定义一帧最多能装多少个 `uint16_t` 脉冲

依赖：

- 结构体数组上限
- 编码缓冲区上限
- 解析缓存上限

输入：

- 编译期常量 `1024`

输出：

- 统一的最大帧容量约束

去向：

- `rf_frame_t.pulse[RF_BUFFER_SIZE]`
- `rf_proto_encode()` 的长度校验
- `rf_proto_parser_consume()` 的长度校验

为什么这样设计：

- 协议层最怕两端对“最大长度”理解不一致
- 这里用一个宏把“模型容量”和“线上的最大负载”钉死

### 3.3 `rf_frame_t`

先看结构体：

```c
typedef struct {
    uint16_t pulse[RF_BUFFER_SIZE];
    uint16_t len;
} rf_frame_t;
```

这段的作用是：

- 定义“一帧脉冲数据”的共享内存模型

依赖：

- `RF_BUFFER_SIZE`
- `uint16_t`

输入：

- 采集侧的脉冲宽度序列
- 接收侧解包后的脉冲宽度序列

输出：

- 一个可以被 encode/decode 两端共同理解的帧对象

去向：

- hardware 侧：`RF_Capture` 产出它，`RF_Uart` 消费它
- master 侧：驱动/应用层消费它，业务解码器继续处理它

为什么这样设计：

- `pulse[]` 保存真实脉冲宽度，不做提前业务化
- `len` 单独记录有效元素数，避免 sentinel 风格的歧义

你要抓住的点是：

- 这不是“协议包结构体”
- 这是“脉冲数据模型”

它描述的是语义层的帧，不是线上字节流本身。

### 2. 资源生命周期与所有权

这段的作用是把“谁创建、谁持有、谁释放、谁转交”说死。你要抓住的点是：`rf_frame_t` 和 `rf_proto_parser_t` 都不是协议层偷偷帮你分配、偷偷帮你销毁的对象。

| 对象 | 创建 | 持有 | 释放 | 转交 |
|---|---|---|---|---|
| `rf_frame_t` | 通常由上游采集侧、调用方栈上或堆上创建 | 创建者持有到 `encode` 或业务消费完成 | 创建者负责释放或出栈 | 作为 `rf_proto_encode()` 的输入传给编码层；也作为 `parser` 的输出交给上层 |
| `rf_proto_parser_t` | 由 hardware 侧调用方创建并初始化 | 调用方跨字节持有同一个 parser 实例 | 调用方负责销毁或让其出作用域 | 不“转交所有权”，只在 `consume()` 中更新内部状态 |
| 编码输出 `out` buffer | 由调用方准备 | 调用方持有容量和生命周期 | 调用方释放 | 只被 `rf_proto_encode()` 写入，不被协议层接管 |
| 接收侧 buffer/queue | 由上游通信层或驱动层创建 | 队列所有者持有 | 队列所有者释放 | 协议层只消费其中的字节，不接管队列本身 |

顺着这张表再看一遍实现，就会很清楚：

1. `rf_frame_t` 不是协议层的临时中间件，它就是业务帧本体。
2. `rf_proto_parser_t` 是状态机上下文，不是一次性函数局部变量。
3. `rf_proto_encode()` 不申请内存，也不回收内存，只写调用方给的 `out`。
4. `rf_proto_parser_consume()` 不回收队列，它只决定当前字节是推进状态，还是把当前帧作废。
5. buffer/queue 的所有权边界不在协议层里，协议层只认“输入”和“输出”。

不是 X，而是 Y：

- 不是“协议层帮你管理对象生命周期”。
- 而是“协议层只定义对象在什么时刻有效、什么时刻失效”。

### 3.4 hardware 侧额外 ABI

hardware 头文件比 master 多了这一段：

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
```

#### `rf_parse_state_t`

作用：

- 描述字节流解析过程所处的阶段

依赖：

- 帧格式固定为 `SYNC + LEN + PAYLOAD + CRC`

输入：

- 每一个串口字节

输出：

- 状态机迁移

去向：

- `rf_proto_parser_consume()`

为什么这样设计：

- 流式输入必须是状态机，而不是一次性 `memcpy`
- 逐字节消费才能处理噪声、半包、粘包和错位

#### `rf_proto_parser_t`

作用：

- 保存 parser 的运行时上下文

字段逐个看：

| 字段 | 作用 | 依赖 | 去向 |
|---|---|---|---|
| `state` | 当前解析阶段 | `rf_parse_state_t` | `rf_proto_parser_consume()` |
| `expected_pulses` | 期望的脉冲数 | 长度字段 | payload 收集和最终还原 |
| `payload_index` | 已收集字节数 | payload 累积过程 | 边界判断 |
| `payload[RF_BUFFER_SIZE * 2u]` | 原始脉冲字节缓存 | 每个脉冲 2 字节 | 最终重组为 `rf_frame_t.pulse[]` |
| `crc` | 累积校验值 | LEN + PAYLOAD | 与尾部 CRC 比较 |

为什么 payload 用 `uint8_t[]`：

- parser 是按字节流工作，不是按 `uint16_t` 数组工作
- 先缓存原始字节，再在 CRC 通过后重组，这样更贴近串口输入模型

不是 X，而是 Y：

- 不是“先直接写成 `uint16_t` 再校验”
- 而是“先按线上的字节形式收齐，再做结构化还原”

---

## 4. master/common/rf_protocol.c

### 4.1 `rf_proto_crc8()`

先看代码：

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

这段的作用是：

- 对一段字节做逐字节 XOR 累积

依赖：

- `uint8_t` 输入数组
- `size_t` 长度

输入：

- 任意字节段

输出：

- 一个 8 位校验值

去向：

- `rf_proto_encode()` 用它生成尾部 CRC

为什么这样设计：

- 计算成本低
- 逐字节接收端也能同步重算
- 适合协议层的轻量一致性检查

你要抓住的点是：

- 这个函数名叫 `crc8`
- 但实现本质上不是多项式 CRC，而是 XOR 累积

换句话说：

- 不是“工业级强校验”
- 而是“协议级轻量完整性检查”

这也是为什么 master/hardware 两端必须保持完全一致：一个字节错了，结果就不同。

### 4.2 `rf_proto_encode()`

先看代码：

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

这段的作用是：

- 把 `rf_frame_t` 序列化成线上字节流

依赖：

- `RF_PROTO_SYNC0`
- `RF_PROTO_SYNC1`
- `RF_BUFFER_SIZE`
- `rf_proto_crc8()`
- `rf_frame_t`

输入：

- `frame`: 待发送脉冲帧
- `out`: 输出缓冲区
- `out_capacity`: 输出缓冲区容量

输出：

- 成功时返回实际写入长度
- 失败时返回 `0`

去向：

- hardware 侧传给 UART 模块
- master/pc_sim 侧也可以复用同样的编码逻辑做回环/测试

为什么这样设计：

- 编码函数必须只做一件事：确定性地把结构化帧压成字节流
- 失败路径统一返回 `0`，调用方只需要看“是否大于 0”

逐步拆：

#### 1. 空指针和长度校验

```c
if (frame == NULL || out == NULL) {
    return 0u;
}
if (frame->len == 0u || frame->len > RF_BUFFER_SIZE) {
    return 0u;
}
```

作用：

- 防止非法输入继续写缓冲区

依赖：

- `rf_frame_t.len` 的有效范围约束

输入：

- 指针参数与帧长度

输出：

- 失败直接返回

去向：

- 上层判断封包是否可发送

为什么这样设计：

- 协议层不替调用方吞掉错误
- 这里宁可早失败，也不要生成一包看起来“像样”但实际非法的字节流

#### 2. 计算总长度

```c
bytes = (uint16_t)(frame->len * 2u);
total = (size_t)2u + 2u + bytes + 1u;
```

作用：

- 计算最终包长

依赖：

- 每个脉冲固定占 2 字节
- 前缀有 2 字节同步字
- 长度字段占 2 字节
- 尾部有 1 字节 CRC

输入：

- `frame->len`

输出：

- `total`

去向：

- 容量检查
- 返回值

为什么这样设计：

- 这是一个完全可预估的定长格式
- 先算总长度，才能在写入前判断缓冲区是否足够

总长度公式就是：

```text
2(sync) + 2(len) + 2*len(payload) + 1(crc)
```

`len = 1024` 时，最大包长是 `2053` 字节。

#### 3. 写同步字与长度

```c
out[0] = RF_PROTO_SYNC0;
out[1] = RF_PROTO_SYNC1;
out[2] = (uint8_t)(frame->len & 0xFFu);
out[3] = (uint8_t)((frame->len >> 8u) & 0xFFu);
```

作用：

- 生成帧头与长度字段

依赖：

- 小端序长度定义

输入：

- `frame->len`

输出：

- `AA 55 LEN_LO LEN_HI`

去向：

- 线上的固定协议头

为什么这样设计：

- 同步字负责找边界
- 长度字段负责告诉接收端 payload 有多长

你要抓住的点是：

- 这不是“可选字段”
- 这是 parser 必须依赖的最小元数据

#### 4. 写 payload

```c
for (i = 0u; i < frame->len; ++i) {
    const uint16_t p = frame->pulse[i];
    const size_t off = (size_t)4u + (size_t)i * 2u;
    out[off] = (uint8_t)(p & 0xFFu);
    out[off + 1u] = (uint8_t)((p >> 8u) & 0xFFu);
}
```

作用：

- 把每个 `uint16_t` 脉冲按小端写入字节流

依赖：

- `frame->pulse[]`
- `frame->len`

输入：

- 脉冲数组

输出：

- `len * 2` 字节的 payload

去向：

- CRC 计算输入
- 串口最终发送内容

为什么这样设计：

- 主机和 MCU 都默认低字节在前
- 这使得“数值语义”和“传输字节顺序”一一对应

不是 X，而是 Y：

- 不是把脉冲转成文本
- 而是保持二进制原样传输，减少额外解释层

#### 5. 计算 CRC

```c
crc = rf_proto_crc8(&out[2], (size_t)2u + bytes);
```

作用：

- 对 `LEN + PAYLOAD` 做 XOR 校验

依赖：

- 已写入的长度字段和 payload

输入：

- 从 `out[2]` 开始的连续字节

输出：

- 一个尾部 CRC 字节

去向：

- `out[4 + bytes]`

为什么这样设计：

- 校验范围不包含同步字
- 同步字用于重锁定，长度与 payload 才是内容一致性检查对象

#### 6. 写 CRC 并返回

```c
out[4u + bytes] = crc;
return total;
```

作用：

- 完成封包

依赖：

- 前面所有写入步骤成功

输入：

- `crc`

输出：

- 完整协议包长度

去向：

- 调用方直接发送 `out[0..total-1]`

为什么这样设计：

- 返回总长度比返回布尔值更实用
- 调用方不需要重新计算包长

---

## 5. hardware/Hardware/RF_Protocol.c

hardware 侧先看结论：

- `rf_proto_crc8()` 和 `rf_proto_encode()` 与 master/common 的行为一致
- 额外增加了 `rf_proto_parser_init()` 和 `rf_proto_parser_consume()`

这说明什么：

- 编码协议是共享契约
- parser 是 hardware 侧补齐的反向能力

### 5.1 `rf_proto_parser_init()`

先看代码：

```c
void rf_proto_parser_init(rf_proto_parser_t *parser) {
    if (parser == NULL) {
        return;
    }
    memset(parser, 0, sizeof(*parser));
    parser->state = RF_PARSE_SYNC0;
}
```

这段的作用是：

- 清零 parser 上下文
- 把状态机重置到起点

依赖：

- `memset`
- `rf_proto_parser_t`

输入：

- parser 指针

输出：

- 一个可以重新从 `SYNC0` 开始消费字节流的状态机

去向：

- 初始化阶段
- 错误恢复阶段
- 成功解析一帧后的复位阶段

为什么这样设计：

- parser 的职责不是“记住历史”，而是“尽快回到可消费状态”
- 每次异常后直接重置，比保留半截脏状态更安全

你要抓住的点是：

- 这是一个状态机的“软复位”
- 不是单纯的内存清零

### 5.2 `rf_proto_parser_consume()`

先看代码：

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

这段的作用是：

- 按字节消费串口数据
- 识别完整帧
- 校验通过后还原出 `rf_frame_t`

依赖：

- `rf_parse_state_t`
- `rf_proto_parser_t`
- `rf_frame_t`
- `RF_PROTO_SYNC0`
- `RF_PROTO_SYNC1`
- `RF_BUFFER_SIZE`
- `rf_proto_parser_init()`

输入：

- `byte`：当前串口字节
- `parser`：状态机上下文
- `out_frame`：输出帧对象

输出：

- `1`：成功解析出一帧
- `0`：还在解析中
- `-1`：出错并已复位

去向：

- 任何以字节流为输入的上层消费逻辑

为什么这样设计：

- 串口输入天然是流，不是包
- parser 必须容忍噪声、半包和粘包
- 解析函数的返回值设计成三态，调用方不用猜

逐状态看。

#### `RF_PARSE_SYNC0`

```c
case RF_PARSE_SYNC0:
    if (byte == RF_PROTO_SYNC0) {
        parser->state = RF_PARSE_SYNC1;
    }
    break;
```

作用：

- 等待第一个同步字节

为什么这样设计：

- 任何一帧的起点都先从 `0xAA` 开始
- 没看到起点前，其他字节都只能视为噪声

#### `RF_PARSE_SYNC1`

```c
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
```

作用：

- 等待第二个同步字节
- 成功后顺手清空本帧的运行状态

为什么这样设计：

- 如果当前字节又是 `0xAA`，parser 保留它作为下一次同步起点的可能
- 这就是“不是全丢，而是尽量保留重同步机会”

你要抓住的点是：

- 这里不是简单的“错了就重置”
- 而是“尽量不浪费连续的同步字节”

#### `RF_PARSE_LEN0`

```c
case RF_PARSE_LEN0:
    parser->expected_pulses = byte;
    parser->crc = byte;
    parser->state = RF_PARSE_LEN1;
    break;
```

作用：

- 收集长度低字节

依赖：

- 小端长度定义

为什么这样设计：

- 先把低字节放进 `expected_pulses`
- 同时把它作为 CRC 初值，和编码端保持一致

#### `RF_PARSE_LEN1`

```c
case RF_PARSE_LEN1:
    parser->expected_pulses |= (uint16_t)((uint16_t)byte << 8u);
    parser->crc ^= byte;
    if (parser->expected_pulses == 0u || parser->expected_pulses > RF_BUFFER_SIZE) {
        rf_proto_parser_init(parser);
        return -1;
    }
    parser->state = RF_PARSE_PAYLOAD;
    break;
```

作用：

- 收集长度高字节
- 校验长度合法性

依赖：

- `RF_BUFFER_SIZE`

为什么这样设计：

- 长度一旦非法，后面的 payload 都没有意义
- 这里提前失败，避免继续吃垃圾字节

不是 X，而是 Y：

- 不是“读完 payload 再检查长度”
- 而是“长度字段一出错就立即拒绝”

#### `RF_PARSE_PAYLOAD`

```c
case RF_PARSE_PAYLOAD:
    parser->payload[parser->payload_index++] = byte;
    parser->crc ^= byte;
    if (parser->payload_index >= (uint16_t)(parser->expected_pulses * 2u)) {
        parser->state = RF_PARSE_CRC;
    }
    break;
```

作用：

- 累积 payload 字节
- 同步更新 XOR 校验

为什么这样设计：

- 一边收包一边校验，成本最低
- 不需要在最后再把整个 payload 重新扫一遍

#### `RF_PARSE_CRC`

```c
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
```

作用：

- 校验尾部 CRC
- 把原始字节重组为 `rf_frame_t`

为什么这样设计：

- 只有 CRC 过了，才把字节流提升回结构化帧
- 这避免了半包污染上层业务

你要抓住的点是：

- parser 的输出不是“原始包”
- 而是“已经验证过的 `rf_frame_t`”

### 3. 解析边界条件

这段的作用是把异常路径一次说清楚。你要抓住的点是：parser 不是“尽量修一修”，而是“该丢就丢，该重置就重置”。

| 边界条件 | 这段的作用是 | 实际行为 | 丢弃责任 |
|---|---|---|---|
| 同步字丢失 | 处理字节流里的噪声和错位 | 在 `SYNC0/SYNC1` 状态里继续找同步，不把噪声当有效帧 | parser 自己丢弃当前搜索上下文 |
| 长度非法 | 防止越界和假包继续推进 | `expected_pulses == 0` 或 `> RF_BUFFER_SIZE` 时立即 `rf_proto_parser_init()` 并返回 `-1` | parser 直接判死，不把残包交上去 |
| CRC 错误 | 防止错误 payload 进入上层业务 | 校验失败时立刻重置状态机并返回 `-1` | parser 丢弃整帧，上层不要尝试修复半包 |
| 恢复路径 | 让下一帧可以重新同步 | 每次失败后都回到 `RF_PARSE_SYNC0`，重新等待下一次同步字 | parser 负责恢复到可继续消费的起点 |
| 上层可见性 | 约束错误传播方式 | 只有成功时才返回 `1` 并输出 `rf_frame_t` | 上层只处理完整帧，不处理半成品 |

把这几条合起来看，结论很直接：

1. 同步字丢了，先继续找同步，不是直接把整个通道判死。
2. 长度不合法，立即丢帧，不要继续吃 payload。
3. CRC 不对，整帧作废，不要把半包交给业务层。
4. 恢复动作永远是 reset 到 `SYNC0`，不是“在当前坏状态里凑合往前走”。
5. 丢弃责任在 parser，业务层只接收完整帧，不接收修补建议。

---

## 6. master/hardware 对齐关系

### 6.1 先看对齐表

| 项 | master/common | hardware/Hardware | 是否对齐 |
|---|---|---|---|
| `RF_PROTO_SYNC0` | `0xAAu` | `0xAAu` | 是 |
| `RF_PROTO_SYNC1` | `0x55u` | `0x55u` | 是 |
| `RF_BUFFER_SIZE` | `1024u` | `1024u` | 是 |
| `rf_frame_t` | `pulse[] + len` | `pulse[] + len` | 是 |
| `rf_proto_crc8()` | XOR 累积 | XOR 累积 | 是 |
| `rf_proto_encode()` | 结构化帧 -> 字节流 | 结构化帧 -> 字节流 | 是 |
| `rf_parse_state_t` | 无 | 有 | hardware 扩展 |
| `rf_proto_parser_t` | 无 | 有 | hardware 扩展 |
| `rf_proto_parser_init()` | 无 | 有 | hardware 扩展 |
| `rf_proto_parser_consume()` | 无 | 有 | hardware 扩展 |

### 6.2 真正对齐的是什么

不是“文件内容完全一样”。

而是下面这几个协议事实完全一样：

1. 同一对同步字
2. 同一份长度定义
3. 同一份 payload 小端布局
4. 同一份 XOR 校验范围
5. 同一份 `rf_frame_t` 语义

这就是 master/hardware 能对齐的根本原因。

### 6.3 为什么 hardware 多出 parser

因为 hardware 侧天然会面对“字节流输入”这一类问题：

- 串口来的可能是连续字节
- 可能半包
- 可能错位
- 可能要做镜像验证

所以 hardware 侧补了一个流式状态机。

但是你要注意：

- hardware 侧 live transmit 的主链路重点仍然是 `RF_Capture -> rf_frame_t -> rf_proto_encode -> UART`
- parser 是协议镜像和验证能力，不是当前发射主链路的核心出口

### 6.4 对齐方式不是共享实现，而是共享契约

这句话最重要。

不是 X，而是 Y：

- 不是“master 和 hardware 通过 include 同一个路径实现一致”
- 而是“两个目录各自保留实现，但共同遵守同一份字节契约”

这给后续维护带来一个直接结论：

- 只改一端是不够的
- 改 `SYNC`、`LEN`、`payload` 排布、CRC 规则时，必须同步检查 master/common、hardware/Hardware 以及所有依赖文档

---

## 7. 数据流总图

### 7.1 hardware 侧主链路

```text
RF_Capture.c 采集到脉冲
  -> 形成 rf_frame_t
  -> RF_Uart_SendFrame()
  -> rf_proto_encode()
  -> AA 55 + LEN + PAYLOAD + CRC
  -> UART 发送
```

这条链路的作用是：

- 把现场采到的脉冲宽度，稳定变成可上送的协议字节流

依赖：

- `rf_frame_t`
- `rf_proto_encode()`

输入：

- 真实脉冲序列

输出：

- 串口字节流

去向：

- 上位机/主控接收端

### 7.2 hardware 侧镜像链路

```text
UART 字节流
  -> rf_proto_parser_init()
  -> rf_proto_parser_consume()
  -> rf_frame_t
```

这条链路的作用是：

- 把线上的字节流按协议镜像回结构化帧

依赖：

- parser 状态机
- `rf_frame_t`

输入：

- 连续字节

输出：

- 一帧经过校验的 `rf_frame_t`

去向：

- 解析验证
- 单元测试
- 协议一致性检查

### 7.3 master 侧消费链路

master 侧相关细节不在这份文档展开，但契约关系是固定的：

- 驱动/应用拿到的是 `rf_frame_t`
- 后续业务解码继续消费这个统一模型

如果你要继续读更上层的链路，可以看：

- [project2_master/README.md](D:/project/repos/project2/project2_master/README.md)
- [project2_master/docs/project2_master_userland_deep_dive.md](D:/project/repos/project2/project2_master/docs/project2_master_userland_deep_dive.md)
- [project2_master/docs/project2_master_driver_deep_dive.md](D:/project/repos/project2/project2_master/docs/project2_master_driver_deep_dive.md)
- [project2_hardware/README.md](D:/project/repos/project2/project2_hardware/README.md)
- [project2_hardware/Hardware/MASTER_INTEGRATION.md](D:/project/repos/project2/project2_hardware/Hardware/MASTER_INTEGRATION.md)

这里只保留一句话：

> `rf_frame_t` 是上层业务的统一入口，而 `rf_protocol.*` 只负责让它在字节流里可信地往返。

---

## 8. 逐项对照结论

### 8.1 这份协议的边界

| 层次 | 负责什么 | 不负责什么 |
|---|---|---|
| `rf_protocol.h/c` | 帧模型、编码、校验、镜像解析 | EV1527 业务解码 |
| `RF_Capture.c` | 采集脉冲并组织成帧 | 协议包格式定义 |
| `RF_Uart.c` | 把协议包送上串口 | 脉冲采集 |
| master 上层解码 | 把 `rf_frame_t` 继续解释为业务结果 | 串口字节帧定义本身 |

### 8.2 最敏感的几个约束

| 约束 | 为什么敏感 |
|---|---|
| `SYNC0/SYNC1` | 一变就无法重同步 |
| `RF_BUFFER_SIZE` | 一变就影响最大帧长和缓存分配 |
| payload 小端 | 一变就两端数值解释不一致 |
| CRC 覆盖范围 | 一变就校验结果不一致 |
| parser 返回值语义 | 一变就影响上层消费方式 |

### 8.3 你要抓住的最终结论

这份协议不是“串口里传几个字节”这么简单。

它的本质是：

1. 用 `rf_frame_t` 把脉冲序列抽象成稳定 ABI
2. 用 `rf_proto_encode()` 把 ABI 变成可传输字节流
3. 用 hardware 侧 parser 证明这套字节流可以被反向稳定恢复
4. 用同一套 `SYNC + LEN + PAYLOAD + CRC` 契约，把 master 和 hardware 锁在一起

---

## 9. 最后一眼只看代码

如果你只想记最核心的结构，直接看下面的 `### 4. 最后一眼只看代码`。

### 4. 最后一眼只看代码

这段的作用是把最关键的骨架压到最短。你要抓住的点是：这套协议最核心的不是“多了多少处理分支”，而是“同一份 `rf_frame_t` 怎么稳定往返”。

```c
#define RF_PROTO_SYNC0 0xAAu
#define RF_PROTO_SYNC1 0x55u
#define RF_BUFFER_SIZE 1024u

typedef struct {
    uint16_t pulse[RF_BUFFER_SIZE];
    uint16_t len;
} rf_frame_t;
```

```c
size_t rf_proto_encode(const rf_frame_t *frame, uint8_t *out, size_t out_capacity);
void rf_proto_parser_init(rf_proto_parser_t *parser);
int rf_proto_parser_consume(rf_proto_parser_t *parser, uint8_t byte, rf_frame_t *out_frame);
```

```c
out[0] = RF_PROTO_SYNC0;
out[1] = RF_PROTO_SYNC1;
out[2] = (uint8_t)(frame->len & 0xFFu);
out[3] = (uint8_t)((frame->len >> 8u) & 0xFFu);

if (parser->expected_pulses == 0u || parser->expected_pulses > RF_BUFFER_SIZE) {
    rf_proto_parser_init(parser);
    return -1;
}
```

最后要记住什么：

- `rf_frame_t` 定义数据长什么样。
- `rf_proto_encode()` 负责把它稳定写成字节流。
- `rf_proto_parser_consume()` 负责把字节流稳定捞回 `rf_frame_t`。
- 不是“字节流里碰运气”，而是“按同一套契约往返搬运”。
