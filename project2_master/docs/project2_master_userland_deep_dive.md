# project2_master 用户态深读

本文只讲 `project2_master/linux_app` 这条真实 master 用户态链路：
`main.c`、`rf_source.c/h`、`rf_epoll.c/h`、`rf_decode.c/h`、`rf_decode_c.c/h`。
不展开 `pc sim`、vendor、生成文件。

先抓住一句话：

```text
main()
  -> 打开 rf_source
  -> 配置 rf_epoll
  -> epoll_wait 事件泵
      -> read 驱动帧
      -> 搬运成 rf_frame_t
      -> on_rf_frame()
          -> rf_decode_frame()
              -> rf_decode_ev1527_c_with_stats()
                  -> build_runs_from_frame()
                  -> decode_best_from_runs()
          -> 低置信度过滤
          -> 稳定分组 / 近邻合并
          -> 重复抑制
          -> printf("[RF] ...")
      -> on_drv_stats()
          -> ioctl 读取驱动统计
  -> 打印用户态汇总统计
  -> rf_source_close()
```

你要抓住的点是：`rf_source` 只负责“怎么打开设备”，`rf_epoll` 只负责“怎么持续收帧”，`rf_decode` 只负责“把一帧变成可打印结果并记统计”，`rf_decode_c` 才是 EV1527 识别算法本体。`main.c` 不做底层读写，它是策略层和状态中心。

## 0. 阅读路线图

这段先给你一个固定的读法：不要从细节函数一路乱跳，而是按 `main -> source -> epoll -> decode -> publish` 的顺序往下压。

- 先读 `main.c`
  - 目标不是背参数，而是先把“策略层 + 状态中心”记住。
  - 你要抓住的点是：哪些状态留在 `main.c`，哪些只是下发给下层。
- 再读 `rf_source.c/h`
  - 目标是确认输入口怎么收紧，`fd` 怎么打开、怎么关。
  - 你要抓住的点是：它不是业务层，只是设备入口壳。
- 再读 `rf_epoll.c/h`
  - 目标是看事件泵怎么持续喂帧，哪里会阻塞，哪里会退出。
  - 你要抓住的点是：`read -> consume -> callback` 不是一步到位，而是一段可回压的节奏。
- 再读 `rf_decode.c/h`
  - 目标是确认“单帧结果怎么标准化”，以及统计怎么分层记账。
  - 你要抓住的点是：它是适配层，不是算法本体。
- 最后读 `rf_decode_c.c/h`
  - 目标是看 EV1527 识别本体怎么做结构筛选、时序打分和候选选择。
  - 你要抓住的点是：这里才是“像不像这一码”的核心判断。

不是先钻 `build_runs_from_frame()`，而是先知道一帧是怎么从 `main.c` 被一路推到 `publish` 的。这样你后面再回来看细节，就知道每一层是在“收口”还是在“放行”。

## 1. `main.c`：主循环不是循环本身，而是整条状态链

### 1.0 文件职责与函数索引

这段先把文件职责排一下，避免你在细节里把层次看反。

- `main.c`
  - 负责参数解析、策略阈值、状态保存、发布、汇总打印、退出清理。
  - 先读的函数：`main()`、`on_rf_frame()`、`on_drv_stats()`
  - 不是底层读写，而是策略层和状态中心。
- `rf_source.c/h`
  - 负责设备白名单、字符设备校验、`fd` 打开和关闭。
  - 先读的函数：`rf_source_is_supported_path()`、`rf_source_open()`、`rf_source_close()`
  - 只是薄封装，不做业务决策。
- `rf_epoll.c/h`
  - 负责事件泵、`epoll_wait()`、驱动帧搬运、定时统计回调。
  - 先读的函数：`consume_frames()`、`rf_epoll_run()`
  - 只是薄封装，不决定业务是否继续。
- `rf_decode.c/h`
  - 负责把算法结果标准化成统一 packet，并顺手记调用统计。
  - 先读的函数：`rf_decode_frame()`、`rf_decode_get_runtime_stats()`、`rf_decode_get_last_call_stats()`
  - 只是包装层，不是解码本体。
- `rf_decode_c.c/h`
  - 负责 EV1527 识别算法本体，做 run 构造、结构筛选、时序评分、候选选择。
  - 先读的函数：`rf_decode_ev1527_c_with_stats()`、`build_runs_from_frame()`、`decode_best_from_runs()`
  - 这里才是核心逻辑，不是薄封装。

### 1.1 运行时上下文

```c
typedef struct {
    uint32_t frame_seq;
    uint32_t last_publish_seq;
    unsigned last_code;
    uint16_t publish_gap;
    uint16_t stable_repeat;
    uint16_t stable_window;
    uint8_t stable_near_bits;
    uint32_t preferred_code;
    float min_publish_confidence;
    int has_last_code;
    rf_stable_group_t stable_groups[RF_STABLE_GROUP_MAX];
    uint32_t frames_total;
    uint32_t decode_ok;
    uint32_t decode_no_frame;
    uint32_t decode_err;
    uint32_t low_conf_drop;
    uint32_t stable_drop;
    uint32_t dup_drop;
    uint32_t published;
    uint32_t drv_seq_prev;
    uint32_t drv_drop;
    int has_drv_seq;
} app_ctx_t;
```

这段的作用是把 master 用户态的全部“状态变化”集中在一个上下文里。

- 依赖：`rf_decode` 的输出、驱动序号、稳定分组、重复发布阈值。
- 输入：每次 `on_rf_frame()` 的单帧解码结果和驱动序号。
- 输出：新的分类结果、打印行为、统计计数、下一帧决策依据。
- 去向：后续每一帧都会继续读取这些状态，决定要不要发布、怎么发布、发布哪个码。
- 为什么这样设计：不是把每一帧当成独立事件处理，而是把“短时间内是否稳定”“同码是否重复”“驱动有没有丢帧”都做成可累积状态。这样主循环才能做策略，而不是只做一次性解码。

`rf_stable_group_t` 也不是解码器的一部分，而是 `main.c` 的后置稳定化缓存：

- `anchor_code` 是分组锚点
- `best_code` 是组内当前最强候选
- `best_conf` 是组内最高置信度
- `hits` / `target_hits` 是命中和偏好码命中计数
- `last_seq` 用来做窗口淘汰

你要抓住的点是：这层分组不是“识别码”，而是“把已经识别出来的码再做一次去抖和合并”。
这段还要顺手抓住资源所有权的线：`ctx`、`epoll_stats`、最终打印的统计对象都由 `main.c` 持有，下面几层只是借用或者写入。
- `rf_fd` 由 `main.c` 打开并持有，进入 `rf_epoll_config_t` 时只是把句柄传下去。
- `rf_epoll_config_t` 本身是栈上的配置对象，事件泵只读它，不接管它。
- `rf_frame_t` 不是长期对象，它通常是 `rf_epoll` 在 `consume_frames()` 里拼出来，再作为回调参数临时交给 `on_rf_frame()`。
- `rf_decoded_packet_t` 是 `on_rf_frame()` 的局部结果，生命周期只覆盖这次回调，打印和分组都在同一段里完成。
- 统计对象分两类：`epoll_stats`、`decode_stats` 这类是 `main.c` 持有的结果容器；`rf_decode` 内部的全局缓存只是供读取，不是所有权转移。

### 1.2 `main()` 的入口顺序

```c
setvbuf(stdout, NULL, _IONBF, 0);
setvbuf(stderr, NULL, _IONBF, 0);

memset(&ctx, 0, sizeof(ctx));
memset(&epoll_stats, 0, sizeof(epoll_stats));
for (i = 1; i < argc; ++i) {
    ...
}

if (!rf_source_is_supported_path(rf_input)) {
    printf("Unsupported --rf-input: %s (allowed: %s)\n", rf_input, RF_SOURCE_PATH);
    return 1;
}

rf_fd = rf_source_open(rf_input);
if (rf_fd < 0) {
    printf("Open RF input failed: %s\n", rf_input);
    return 2;
}
```

这段的作用是把“程序启动”收束成三步：参数解析、路径校验、设备打开。

- 依赖：`rf_source_is_supported_path()`、`rf_source_open()`。
- 输入：命令行参数和默认 `RF_SOURCE_PATH`。
- 输出：一个合法的 `rf_fd`，或者直接退出。
- 去向：`rf_fd` 进入 `rf_epoll_config_t`，成为后续事件泵的核心输入。
- 为什么这样设计：用户态 master 不是开放式文件读取器，而是只接受固定设备入口。先拦掉非法路径，再打开字符设备，可以避免后面的 epoll 和 decode 在错误输入上空转。

这里的参数是策略开关，不是协议参数：

- `--stable-repeat`：同一组至少要命中几次才发布
- `--stable-window`：分组存活窗口
- `--stable-near-bits`：24 位码的汉明近邻阈值
- `--preferred-code`：合并时偏好输出的码
- `--min-publish-confidence`：最低可发布置信度
- `--publish-gap`：同码最小重发间隔

你要抓住的点是：这些参数全都发生在 `main.c`，说明“发布策略”在用户态而不是解码器内部。

### 1.3 事件泵配置

```c
cfg.rf_fd = rf_fd;
cfg.on_frame = on_rf_frame;
cfg.on_stats = on_drv_stats;
cfg.stats_interval_s = 5;
cfg.stats = &epoll_stats;
cfg.user = &ctx;

printf(
    "rf_gateway running: rf=%s stable=%u near=%u stable_win=%u preferred=0x%06X pub_conf=%.2f gap=%u\n",
    ...
);
epoll_rc = rf_epoll_run(&cfg);
```

这段的作用是把主程序的职责一次性拆成两个回调：一个处理帧，一个处理周期统计。
你要抓住的点是：`main.c` 不是在这里“开始读帧”，而是在这里把所有权和控制权交给事件泵。`cfg.stats = &epoll_stats` 这类赋值也不是转交所有权，只是让下层回调时能写回同一份状态。

- 依赖：`rf_epoll_config_t`、`on_rf_frame()`、`on_drv_stats()`。
- 输入：`rf_fd`、统计间隔、策略参数、上下文指针。
- 输出：交给 `rf_epoll_run()` 的持续事件循环。
- 去向：`rf_epoll` 内部调用 `epoll_wait()`，再回调到 `main.c`。
- 为什么这样设计：不是 `main()` 自己 while-read，而是把“事件泵”单独抽出来。这样读帧逻辑、统计逻辑和策略逻辑就分层了，后续更容易替换输入源或者加定时统计。

### 1.4 `on_rf_frame()`：真正的业务核心

```c
static int on_rf_frame(const rf_frame_t *frame, uint64_t timestamp_ns, uint32_t drv_seq, void *user) {
    ...
    rc = rf_decode_frame(frame, &pkt);
    memset(&call_stats, 0, sizeof(call_stats));
    rf_decode_get_last_call_stats(&call_stats);
    ...
    if (pkt.confidence < ctx->min_publish_confidence) {
        ctx->low_conf_drop++;
        return 0;
    }
    ...
    printf("[RF] addr=%s key=%s conf=%.2f source=%s pulses=%u seq=%u decode_us=%llu pulse_us=", ...);
    ...
}
```

这段的作用是把“原始帧”变成“可发布事件”，并在中间插入所有用户态策略。

#### 1.4.1 先记驱动丢帧

```c
if (ctx->has_drv_seq && drv_seq > ctx->drv_seq_prev) {
    const uint32_t gap = drv_seq - ctx->drv_seq_prev - 1u;
    if (gap > 0u) {
        ctx->drv_drop += gap;
    }
}
ctx->drv_seq_prev = drv_seq;
ctx->has_drv_seq = 1;
```

- 作用：从驱动序号看出中间是否漏帧。
- 依赖：`drv_seq`、`ctx->drv_seq_prev`。
- 输入：本帧驱动序号。
- 输出：`ctx->drv_drop` 累加。
- 去向：最后的 `[RF_STATS]` 汇总。
- 为什么这样设计：这不是协议层丢包，而是驱动 FIFO 或读路径上的缺口统计。它不影响解码，但影响你判断整条链路是否稳定。

#### 1.4.2 再解码

```c
rc = rf_decode_frame(frame, &pkt);
rf_decode_get_last_call_stats(&call_stats);
```

- 作用：把 `rf_frame_t` 转成 `rf_decoded_packet_t`，并取回本次调用耗时。
- 依赖：`rf_decode.c` 的包装层。
- 输入：一帧脉冲数组。
- 输出：`pkt.addr`、`pkt.key`、`pkt.raw_code`、`pkt.confidence`、`pkt.source`。
- 去向：后续过滤、分组、打印。
- 为什么这样设计：解码结果和解码性能被拆成两条线记录。结果用于业务，耗时用于观测。

这里要特别抓住一点：`main.c` 看到的不是原始算法细节，而是一个已经标准化的 packet。

#### 1.4.3 低置信度先丢

```c
if (pkt.confidence < ctx->min_publish_confidence) {
    ctx->low_conf_drop++;
    return 0;
}
```

- 作用：先挡掉“能解出来，但不够稳”的结果。
- 依赖：`min_publish_confidence`。
- 输入：解码器给出的 `confidence`。
- 输出：要么放行，要么计入 `low_conf_drop`。
- 去向：只有通过这层的结果才会进入稳定分组。
- 为什么这样设计：不是让解码器去做策略判断，而是把“算法评分”和“业务可发布阈值”分开。这样阈值能在用户态单独调。

#### 1.4.4 稳定分组不是解码，而是后处理

```c
if (ctx->stable_repeat > 1u) {
    stable_groups_decay(ctx);
    idx = stable_group_find(ctx, pkt.raw_code);
    if (idx < 0) {
        idx = stable_group_alloc(ctx);
        if (idx >= 0) {
            stable_group_seed(...);
        }
        ctx->stable_drop++;
        return 0;
    }
    g = &ctx->stable_groups[idx];
    stable_group_update(g, pkt.raw_code, pkt.confidence, ctx->frame_seq, ctx->preferred_code);
    if (g->hits < ctx->stable_repeat) {
        ctx->stable_drop++;
        return 0;
    }
    if (g->target_hits > 0u) {
        pkt.raw_code = ctx->preferred_code & 0xFFFFFFu;
    } else {
        pkt.raw_code = g->best_code & 0xFFFFFFu;
    }
    snprintf(pkt.addr, sizeof(pkt.addr), "0x%06X", pkt.raw_code & 0xFFFFFFu);
    snprintf(pkt.key, sizeof(pkt.key), "%u", (unsigned)(pkt.raw_code & 0x0Fu));
}
```

这段的作用是把单帧识别结果再压成“稳定事件”。

- 依赖：`stable_window`、`stable_near_bits`、`stable_repeat`、`preferred_code`。
- 输入：`pkt.raw_code`、`pkt.confidence`、当前 `frame_seq`。
- 输出：可能被重写后的 `pkt.raw_code`、`pkt.addr`、`pkt.key`、`pkt.confidence`。
- 去向：后面的重复抑制和打印。
- 为什么这样设计：不是一次识别就立刻上报，而是先把短时抖动、相近码漂移和目标码优先级都消化掉。`stable_groups` 是用户态的“事件平滑器”。

你要抓住的点是：

- `stable_group_find()` 用汉明距离找近邻组
- `stable_group_seed()` 新建组
- `stable_group_update()` 续命和刷新最佳候选
- `stable_groups_decay()` 清掉过期组

这说明稳定化依赖的是“连续帧状态”，不是单帧结果。

#### 1.4.5 重复发布抑制

```c
if (
    ctx->has_last_code &&
    pkt.raw_code == ctx->last_code &&
    (ctx->frame_seq - ctx->last_publish_seq) < (uint32_t)ctx->publish_gap
) {
    ctx->dup_drop++;
    return 0;
}
```

- 作用：防止同一个码在很短时间里连续刷屏。
- 依赖：`last_code`、`last_publish_seq`、`publish_gap`。
- 输入：当前稳定后的 `raw_code`。
- 输出：`dup_drop` 或放行。
- 去向：真正进入 `printf("[RF] ...")` 的只剩非重复结果。
- 为什么这样设计：不是让上游一直发，而是把“可见事件频率”控制在用户态。这对串口/终端打印和上层消费都更稳。

#### 1.4.6 打印不是最后一步，打印后还要更新发布状态

```c
printf("[RF] addr=%s key=%s conf=%.2f source=%s pulses=%u seq=%u decode_us=%llu pulse_us=", ...);
for (uint16_t i = 0; i < frame->len; ++i) {
    printf("%s%u", (i == 0u) ? "" : ",", (unsigned)frame->pulse[i]);
}
printf("\n");
ctx->last_code = pkt.raw_code;
ctx->has_last_code = 1;
ctx->last_publish_seq = ctx->frame_seq;
ctx->published++;
```

- 作用：把最终结果输出成日志，并同步更新“最近发布了什么”。
- 依赖：`pkt`、`call_stats`、`frame->pulse[]`。
- 输入：稳定化后的 packet。
- 输出：控制台日志和新的发布状态。
- 去向：日志给人看，状态给后续帧做去重。
- 为什么这样设计：不是“打印结束就结束”，而是“打印本身也是状态转移的一部分”。`last_code` 和 `last_publish_seq` 就是后续抑制重复的依据。

这里还要再看一眼发布时序：打印和状态更新是一体的，不是“先打完再说”。`ctx->last_code`、`ctx->has_last_code`、`ctx->last_publish_seq`、`ctx->published` 都是在这一步后写回的，这样后面的 `dup_drop` 才有判断基线。

### 1.5 退出后的三类汇总

```c
printf("[RF_STATS] frames_total=%u decode_ok=%u decode_no_frame=%u decode_err=%u low_conf_drop=%u stable_drop=%u dup_drop=%u published=%u drv_drop=%u\n", ...);
printf("[RF_IO_STATS] read_eintr=%u read_eagain=%u read_eof=%u read_error=%u epoll_eintr=%u epoll_error=%u\n", ...);
rf_decode_get_runtime_stats(&decode_stats);
printf("[RF_DECODE_STATS] c_attempts=%u c_accepts=%u c_total_us=%llu c_accept_total_us=%llu\n", ...);
```

这段的作用是把三条链路的统计合在一起看：

- `RF_STATS`：业务层统计，说明帧最终怎么被筛掉或发布
- `RF_IO_STATS`：事件泵和读取层统计，说明输入链路有没有抖动、短读、EOF、EINTR
- `RF_DECODE_STATS`：解码层统计，说明算法被尝试了多少次、接受了多少次、耗时多少

你要抓住的点是：统计不是附属打印，而是这份文档里最能看出层次分工的地方。`drv_drop` 不是 `read_error`，`low_conf_drop` 不是 `decode_no_frame`，`c_accepts` 也不是 `published`。

### 1.6 退出清理顺序

```c
epoll_rc = rf_epoll_run(&cfg);
if (epoll_rc != 0) {
    fprintf(stderr, "[RF_IO] rf_epoll_run exited with rc=%d\n", epoll_rc);
}
...
rf_source_close(rf_fd);
return 0;
```

这段的作用是把“退出”也写成一条明确链路。
你要抓住的点是：先让事件泵自己退干净，再在 `main.c` 里统一打印汇总，最后才关 `rf_fd`。不是每一层都各自静默退出，而是先保住统计状态，再收口资源。

- 依赖：`rf_epoll_run()` 的返回值、`rf_source_close()`。
- 输入：事件泵结束原因和最终统计状态。
- 输出：`epoll_run` 内部关闭 `timerfd` / `epfd`，`main` 最后关闭 `rf_fd`。
- 去向：程序返回给 shell。
- 为什么这样设计：不是在每一层都各自静默退出，而是让事件泵先收尾，再由主程序统一打印汇总，最后再关闭输入源。这样能保证统计先落盘、fd 后释放，排查问题时不会丢链路尾巴。

## 2. `rf_source.c/h`：只做设备白名单和 fd 生命周期

### 2.1 代码

```c
int rf_source_is_supported_path(const char *path) {
    if (path == NULL || path[0] == '\0') {
        return 1;
    }
    return (strcmp(path, RF_SOURCE_PATH) == 0) ? 1 : 0;
}

int rf_source_open(const char *path) {
    const char *resolved = (path == NULL || path[0] == '\0') ? RF_SOURCE_PATH : path;
    if (!rf_source_is_supported_path(resolved)) {
        errno = EINVAL;
        return -1;
    }
    if (rf_source_is_char_device(resolved) == 0) {
        errno = ENOTTY;
        return -1;
    }
    return open(resolved, O_RDONLY | O_NONBLOCK);
}
```

这段的作用是把 master 的输入入口锁死在一个已知字符设备上。

- 依赖：`RF_SOURCE_PATH`、`stat()`、`S_ISCHR()`、`open()`。
- 输入：路径参数。
- 输出：合法 fd 或失败。
- 去向：`main.c` 把这个 fd 交给 `rf_epoll`。
- 为什么这样设计：不是让上层随便传文件名，而是先把“输入源”定义成字符设备。这样 `read()`、`epoll`、`ioctl` 才有语义一致性。

你要抓住的点是：

- `rf_source_is_supported_path()` 不是做安全模型，它只是做路径收口
- `rf_source_is_char_device()` 不是为了好看，而是为了在打开前就排除普通文件
- `rf_source_close()` 只负责 `close(fd)`，不做额外语义

这说明 `rf_source` 不是抽象层，而是极薄的一层设备适配壳。

## 3. `rf_epoll.c/h`：事件泵只负责“持续喂帧”

### 3.1 配置结构

```c
typedef struct {
    int rf_fd;
    rf_epoll_on_frame_fn on_frame;
    rf_epoll_on_stats_fn on_stats;
    int stats_interval_s;
    rf_epoll_stats_t *stats;
    void *user;
} rf_epoll_config_t;
```

这段的作用是把事件泵做成“通用调度器”，而不是绑定具体业务。

- 依赖：`rf_frame_t`、回调函数类型、统计结构体。
- 输入：fd、回调、统计间隔、用户指针。
- 输出：回调驱动的事件流。
- 去向：`main.c` 的 `on_rf_frame()` 和 `on_drv_stats()`。
- 为什么这样设计：不是把业务逻辑写进 epoll 循环里，而是用回调把数据面和观测面拆开。

### 3.2 `consume_frames()`：把驱动帧搬成用户帧

```c
while (1) {
    n = read(cfg->rf_fd, &drv_frame, sizeof(drv_frame));
    if (n < 0) {
        if (errno == EINTR) { ... continue; }
        if (errno == EAGAIN || errno == EWOULDBLOCK) { ... return 0; }
        ... return -1;
    }
    if (n == 0) { ... return -2; }
    if ((size_t)n < sizeof(drv_frame)) { ... continue; }

    memset(&frame, 0, sizeof(frame));
    frame.len = drv_frame.pulse_count;
    if (frame.len > RF_BUFFER_SIZE) {
        frame.len = (uint16_t)RF_BUFFER_SIZE;
    }
    memcpy(frame.pulse, drv_frame.pulse, frame.len * sizeof(uint16_t));

    if (cfg->on_frame != NULL) {
        if (cfg->on_frame(&frame, drv_frame.timestamp_ns, drv_frame.seq, cfg->user) != 0) {
            return 1;
        }
    }
}
```

这段的作用是把驱动层 `struct rf433_frame` 搬运成用户态 `rf_frame_t`，然后交给上层处理。

- 依赖：驱动 `read()` 结果、`RF_BUFFER_SIZE`、`on_frame` 回调。
- 输入：驱动帧、时间戳、序号。
- 输出：用户帧和回调结果。
- 去向：`main.c` 的 `on_rf_frame()`。
- 为什么这样设计：不是让上层直接碰驱动结构，而是先把数据复制成通用的用户态协议帧。这样上层只认 `rf_frame_t`，不关心驱动私有布局。

这里要抓住三种读路径：

- `EINTR`：继续读
- `EAGAIN/EWOULDBLOCK`：当前没数据，退出本轮消费
- `short read` / `EOF` / 其他错误：计入统计并返回失败

这说明 `rf_epoll` 不是“读一次就完”，而是“把设备内可读的数据一次性清空”。

### 3.3 `rf_epoll_run()`：一个 epoll 同时管理数据和统计

```c
epfd = epoll_create1(0);
epoll_ctl(epfd, EPOLL_CTL_ADD, cfg->rf_fd, &ev);
...
if (cfg->on_stats != NULL && cfg->stats_interval_s > 0) {
    timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
    ...
    epoll_ctl(epfd, EPOLL_CTL_ADD, timer_fd, &ev);
}

while (1) {
    nfds = epoll_wait(epfd, events, ..., -1);
    ...
    if (events[i].data.fd == cfg->rf_fd) {
        rc = consume_frames(cfg);
        if (rc != 0) {
            goto out;
        }
    } else if (timer_fd >= 0 && events[i].data.fd == timer_fd) {
        (void)read(timer_fd, &expirations, sizeof(expirations));
        if (cfg->on_stats != NULL) {
            cfg->on_stats(cfg->rf_fd, cfg->user);
        }
    }
}
```

这段的作用是把数据泵和观测泵放在同一个事件循环里。
你要抓住的点是：这里的“回压”是自然形成的，不是显式队列控制。`consume_frames()` 一直读到 `EAGAIN` 才停，说明内核里可读的数据已经清空；如果 `on_frame()` 返回非 0，事件泵就把退出信号往上抛，不再继续喂后面的帧；如果设备读到 EOF 或真错误，`rf_epoll_run()` 也会收掉整个循环。

- 依赖：`epoll`、`timerfd`、`consume_frames()`。
- 输入：`rf_fd` 以及 `stats_interval_s`。
- 输出：连续的帧处理回调和周期统计回调。
- 去向：帧流进 `on_rf_frame()`，统计流进 `on_drv_stats()`。
- 为什么这样设计：不是单开线程去拉统计，也不是在主循环里睡眠轮询，而是让 `epoll` 同时管两类 fd。这样结构简单、时序稳定、没有额外线程同步成本。

你要抓住的点是：

- `EPOLLIN` 触发时才去 `read()`，避免忙等
- `timerfd` 触发时才去查统计，避免频繁 `ioctl`
- `cfg->on_frame()` 返回非 0 时，整个事件泵退出

这说明 `rf_epoll` 只负责“把该喂的东西按事件喂出去”，并不决定业务是否继续。
### 3.4 事件泵与回压时序

这段再把时序说死一点：不是“有事件就打印”，而是“有事件就尽量消化，再决定发不发，发完再回到等待”。

- `epoll_wait()` 唤醒后，先看 `rf_fd` 可不可读。
- 如果可读，就进 `consume_frames()`，把一批驱动帧搬成一批 `rf_frame_t`。
- 每一帧回到 `main.c` 后，先 `decode`，再做低置信度过滤、稳定分组、重复抑制，最后才 `publish`。
- 只要 `read()` 还在继续返回数据，本轮就继续消化；一旦触到 `EAGAIN/EWOULDBLOCK`，说明本轮收干净了，回去等下一次唤醒。
- 如果 `on_frame()` 在某个业务阈值上返回非 0，说明不是“读不动了”，而是“策略上要停了”，这时事件泵直接退出。
- `timerfd` 触发的是统计回调，不参与帧数据通路，所以它不会改变 `publish` 的节奏，只负责把后台状态暴露出来。

不是 `read -> print -> read`，而是 `read -> consume -> decode -> publish -> wait`。这个顺序一旦看清楚，你就能把“堵塞”“丢帧”“唤醒”“退出”四件事连起来看。

## 4. `rf_decode.c/h`：适配层，不是算法层

### 4.1 代码

```c
int rf_decode_frame(const rf_frame_t *frame, rf_decoded_packet_t *out) {
    ...
    g_rf_last_call_stats.frame_len = frame->len;

    t0 = rf_now_us();
    g_rf_decode_stats.c_attempts++;
    c_rc = rf_decode_ev1527_c_with_stats(frame, &c_result, NULL);
    t1 = rf_now_us();

    g_rf_last_call_stats.c_total_us = t1 - t0;
    g_rf_last_call_stats.total_us = t1 - t0;
    g_rf_last_call_stats.c_confidence = c_result.confidence;
    g_rf_decode_stats.c_total_us += g_rf_last_call_stats.c_total_us;

    if (c_rc == 0) {
        snprintf(out->addr, sizeof(out->addr), "0x%06X", c_result.raw_code);
        snprintf(out->key, sizeof(out->key), "%u", (unsigned)c_result.button4);
        snprintf(out->source, sizeof(out->source), "c");
        out->raw_code = c_result.raw_code;
        out->confidence = c_result.confidence;
        g_rf_last_call_stats.c_ok = 1;
        g_rf_decode_stats.c_accepts++;
        g_rf_decode_stats.c_accept_total_us += g_rf_last_call_stats.c_total_us;
        return 0;
    }

    g_rf_last_call_stats.rc = RF_DECODE_RC_NO_FRAME;
    return RF_DECODE_RC_NO_FRAME;
}
```

这段的作用是把算法结果包装成统一输出，并记录本次调用的性能统计。

- 依赖：`rf_decode_ev1527_c_with_stats()`、`clock_gettime()`、全局统计缓存。
- 输入：一帧 `rf_frame_t`。
- 输出：标准化 packet 和调用统计。
- 去向：`main.c` 的 `on_rf_frame()`。
- 为什么这样设计：不是让业务层直接接触 EV1527 算法结构，而是让它只看到统一 packet。这样未来如果换 decode 算法，`main.c` 的业务逻辑不用改。

你要抓住的点是：

- `rf_decode` 不是“第二套算法”，而是“结果适配器 + 统计器”
- 当前实现把成功结果标成 `source="c"`
- 当前实现把算法失败统一折叠成 `RF_DECODE_RC_NO_FRAME`

这说明 `main.c` 里看到的 `decode_no_frame`，在当前实现下基本对应“这帧没有被 EV1527 C 解码器接受”，而不是驱动读失败。

### 4.2 统计联动

```c
void rf_decode_get_runtime_stats(rf_decode_runtime_stats_t *out) {
    if (out == NULL) {
        return;
    }
    *out = g_rf_decode_stats;
}

void rf_decode_get_last_call_stats(rf_decode_last_call_stats_t *out) {
    if (out == NULL) {
        return;
    }
    *out = g_rf_last_call_stats;
}
```

这段的作用是把“累计统计”和“最近一次调用统计”分开。
这也对应着所有权边界：`rf_decode` 内部拿着自己的静态统计缓存，但它不接管上层 packet，也不接管上层输入帧。`main.c` 只是在读这些观测值，不是在和 `rf_decode` 共享可变所有权。

### 4.3 异常路径与退出顺序

这段要单独抓住，因为它决定了统计是不是会丢尾巴。

- `EINTR`
  - 语义是被信号打断，不是读错。
  - 处理方式是继续读或继续等，不要把它当成退出条件。
- `EAGAIN` / `EWOULDBLOCK`
  - 语义是本轮已经把可读数据消化完了。
  - 处理方式是回到 `epoll_wait()`，让下一次事件唤醒再继续。
- `EOF` / `read() == 0`
  - 语义是输入口真的收尾了，事件泵要退出。
  - 处理方式是向上返回，让 `main.c` 进入统一收口。
- 其他真错误
  - 语义是设备通路异常，不能再假装继续。
  - 处理方式是计入错误并退出当前循环。

你要抓住的点是：退出顺序不是“谁先碰到就谁先打印”，而是“先让事件泵停下来，保住 `ctx` 和统计对象，再由 `main.c` 打印汇总，最后关闭 `rf_fd`”。这样 `RF_STATS`、`RF_IO_STATS`、`RF_DECODE_STATS` 打出来时，还是同一份完整状态，不会因为提前 close 或提前清零把尾巴抹掉。

- 依赖：全局统计对象。
- 输入：无。
- 输出：runtime 级统计或 last-call 级统计。
- 去向：`main.c` 在打印和分析时使用。
- 为什么这样设计：不是把所有统计都塞成一个大表，而是把持续趋势和单次观测拆开。前者看长期，后者看当前帧。

## 5. `rf_decode_c.c/h`：EV1527 识别本体

### 5.1 先看数据流

```c
int rf_decode_ev1527_c_with_stats(const rf_frame_t *frame, rf_decode_result_c_t *out, rf_decode_stage_stats_t *stats) {
    rf_run_t runs[RF_BUFFER_SIZE];
    uint16_t run_count = 0u;
    rf_decode_result_c_t best;
    int found = 0;
    int phase = 0;

    for (phase = 1; phase >= 0; --phase) {
        rf_decode_result_c_t phase_best;
        build_runs_from_frame(frame, phase, runs, &run_count);
        if (decode_best_from_runs(runs, run_count, &phase_best, stats) != 0) {
            continue;
        }
        ...
    }
}
```

这段的作用是把一帧脉冲做成可搜索的 run 序列，然后在两种起始极性上都试一遍。

- 依赖：`rf_frame_t`、`rf_run_t`、统计结构体。
- 输入：脉冲宽度数组。
- 输出：最佳候选 `rf_decode_result_c_t`。
- 去向：`rf_decode.c` 的包装层。
- 为什么这样设计：不是假设帧一定从固定高低电平开始，而是两种相位都试。这样更抗输入边界的不确定性。

你要抓住的点是：`rf_decode_c` 是“算法本体”，它不负责打印、不负责发布、不负责重复抑制，它只负责“这一帧像不像 EV1527，像的话最像哪个码”。

### 5.2 `build_runs_from_frame()`：先把脉冲变成 run

```c
static void build_runs_from_frame(const rf_frame_t *frame, int start_level, rf_run_t *runs_out, uint16_t *run_count_out) {
    ...
    for (i = 0u; i < frame->len && i < RF_BUFFER_SIZE; ++i) {
        const uint16_t sample_len = us_to_samples(frame->pulse[i]);
        runs_out[i].level = level;
        runs_out[i].start = cursor;
        runs_out[i].length = sample_len;
        cursor = (uint16_t)(cursor + sample_len);
        level = level ? 0 : 1;
        *run_count_out = (uint16_t)(*run_count_out + 1u);
    }
}
```

这段的作用是把微秒脉宽转成固定采样率下的 run 序列。

- 依赖：`EV1527_SAMPLE_RATE`、`us_to_samples()`。
- 输入：原始脉宽数组和起始电平。
- 输出：run 数组和 run 数量。
- 去向：后面的结构检查和打分。
- 为什么这样设计：不是直接拿脉宽硬怼协议公式，而是先统一成“高/低 run 的时长序列”。这样后面的窗口搜索、周期估计和误差评分都更顺。

### 5.3 `decode_best_from_runs()`：先结构，再时序，再评分

这部分是核心，逻辑顺序非常重要。

#### 5.3.1 先找合法窗口

```c
for (i = 1u; i < (uint16_t)(run_count - (uint16_t)(2u * EV1527_BITS)); ++i) {
    if (runs[i - 1u].level != 1 || runs[i].level != 0) {
        continue;
    }
    sync_high = (float)runs[i - 1u].length;
    sync_low = (float)runs[i].length;
    ...
}
```

这段的作用是先锁定“看起来像同步头”的位置。

- 依赖：run 序列、`EV1527_BITS`。
- 输入：run 数组和运行长度。
- 输出：一个候选起点。
- 去向：后续比特检查和置信度计算。
- 为什么这样设计：不是一上来就算每一位，而是先把明显不对的窗口全部排除。这样计算量更低，误判更少。

#### 5.3.2 再检查位级结构

```c
for (b = 0u; b < (uint16_t)(2u * EV1527_BITS); ++b) {
    const int expected_level = ((b & 1u) == 0u) ? 1 : 0;
    if (runs[bit_start + b].level != expected_level) {
        break;
    }
}
```

这段的作用是确认数据段高低交替结构没乱。

- 依赖：协议位序和 run 极性。
- 输入：候选起点后的连续 run。
- 输出：通过或拒绝该候选。
- 去向：只有通过结构检查才进入时序评分。
- 为什么这样设计：不是只看时间长短，而是把“结构正确性”先作为门槛。EV1527 的核心不是任意脉宽，而是固定的高低组合模式。

#### 5.3.3 再估计时钟

```c
totals[b] = runs[bit_start + 2u * b].length + runs[bit_start + 2u * b + 1u].length;
clk_from_totals = (float)upper_median_u16(totals, EV1527_BITS) / pair_t;
clk_from_sync = sync_low / EV1527_PROFILE_SYNC_LOW_T;
clk = 0.80f * clk_from_totals + 0.20f * clk_from_sync;
```

这段的作用是从数据位和同步头两边共同估计基础时钟。

- 依赖：`upper_median_u16()`、同步头长度、每位总时长。
- 输入：当前候选窗口的 run 长度。
- 输出：`clk`。
- 去向：后面每个 bit 的误差评价。
- 为什么这样设计：不是完全相信同步头，也不是完全相信数据位，而是做加权融合。这样更抗量化误差和局部抖动。

#### 5.3.4 再按位计算双假设误差

```c
const float err0 =
    rel_err_quantized(hi, EV1527_PROFILE_BIT_SHORT_T * clk) +
    rel_err_quantized(lo, EV1527_PROFILE_BIT_LONG_T * clk);
const float err1 =
    rel_err_quantized(hi, EV1527_PROFILE_BIT_LONG_T * clk) +
    rel_err_quantized(lo, EV1527_PROFILE_BIT_SHORT_T * clk);
...
if (err1 < err0) {
    code = (code << 1u) | 1u;
} else {
    code = (code << 1u);
}
```

这段的作用是对每一位同时试 `0` 和 `1` 两种解释，选误差更小的一边。

- 依赖：短脉冲/长脉冲比例模板。
- 输入：每位高低 run 长度。
- 输出：逐位拼出的 `code` 和 bit 误差。
- 去向：候选打分。
- 为什么这样设计：不是假设信号一定完美符合某一边，而是让误差最小的假设胜出。这样更适合真实采样里有噪声、有量化误差的情况。

#### 5.3.5 最后不是“能解就行”，而是“分数够不够高”

```c
raw_conf = 1.0f - (
    0.41f * bit_norm +
    0.19f * jitter_norm +
    0.14f * spread_norm +
    0.03f * outlier_norm +
    0.08f * sync_error_norm +
    0.05f * sync_ratio_penalty +
    0.10f * low_penalty
);
conf = fmaxf(0.0f, raw_conf) * fminf(1.0f, low_ratio / 2.2f);
```

这段的作用是把多个质量维度压成一个 `confidence`。

- 依赖：bit error、周期抖动、同步误差、低脉冲比例等指标。
- 输入：候选窗口的整体统计。
- 输出：候选置信度。
- 去向：`best_out` 的最终选择。
- 为什么这样设计：不是只看“能不能拼出 24 位”，而是看“这帧是不是整体像 EV1527”。这就是为什么上层还能再做 `min_publish_confidence`。

#### 5.3.6 选择最佳候选

```c
if (!found || conf > best_conf || ... ) {
    best_conf = conf;
    best_out->raw_code = code & 0xFFFFFFu;
    best_out->address20 = (code >> 4u) & 0xFFFFFu;
    best_out->button4 = (uint8_t)(code & 0x0Fu);
    best_out->clk_us = ...
    best_out->confidence = conf;
    best_out->bit_error = bit_error;
    best_out->sync_error = sync_error;
    best_out->period_jitter = period_jitter;
    best_out->start_index = (uint16_t)(i - 1u);
    found = 1;
}
```

这段的作用是把最佳候选的全部可观测量一次性封装出来。

- 依赖：候选比较规则。
- 输入：当前候选分数和历史最佳分数。
- 输出：`rf_decode_result_c_t`。
- 去向：`rf_decode.c` 再包装成打印结果。
- 为什么这样设计：不是只保留 `raw_code`，而是把为什么选中它的依据也保留下来。这样上层能做更细的策略和调试。

### 5.4 `rf_decode_stage_stats_t` 是两级门控的可观测点

```c
typedef struct {
    uint16_t step1_structural;
    uint16_t step2_timing;
} rf_decode_stage_stats_t;
```

这段的作用是给算法内部加两个阶段计数。

- `step1_structural`：候选先通过了结构筛选
- `step2_timing`：候选再通过了时序评分

你要抓住的点是：这两个计数只说明“算法走到了哪一步”，不是业务发布成功数。

## 6. 这三层统计怎么联动

### 6.1 驱动统计

`on_drv_stats()` 里做的是：

```c
ioctl(rf_fd, RF433_IOC_GET_STATS, &drv_stats)
ioctl(rf_fd, RF433_IOC_GET_STATUS, &drv_status)
```

然后打印：

- `frame_ok`
- `crc_err`
- `len_err`
- `drop_cnt`
- `online`
- `seq`
- `queue_depth`
- `queue_capacity`

这段的作用是回答“驱动层是不是健康”。

### 6.2 用户态业务统计

`[RF_STATS]` 回答的是“用户态处理完后，最终流向哪里了”。

- `frames_total`：收到了多少帧
- `decode_ok`：成功进入 decode 的帧数
- `decode_no_frame`：decode 认为这帧不成立
- `low_conf_drop`：解出来但置信度太低
- `stable_drop`：稳定化阶段先压住了
- `dup_drop`：重复发布被抑制
- `published`：最终发出了多少条
- `drv_drop`：驱动序号缺口

你要抓住的点是：这一组统计能把一条帧从进入用户态到真正打印，在哪一步被丢掉全部拆出来。

### 6.3 解码统计

`[RF_DECODE_STATS]` 回答的是“算法层被尝试了多少次、接受了多少次、花了多久”。

- `c_attempts`：尝试次数
- `c_accepts`：被 EV1527 C 算法接受的次数
- `c_total_us`：总耗时
- `c_accept_total_us`：接受帧的耗时累计

这三层放在一起看，才能判断问题在驱动、事件泵、算法，还是用户态策略。

## 7. 最后再把关系说死

不是：

- `rf_source` 在做解码
- `rf_epoll` 在做协议识别
- `rf_decode` 和 `rf_decode_c` 是两套平行的业务实现

而是：

- `rf_source` 只负责把字符设备入口收紧
- `rf_epoll` 只负责把设备上的可读事件持续泵出来
- `rf_decode` 只负责把算法结果标准化并记账
- `rf_decode_c` 才是当前真正的 EV1527 识别算法
- `main.c` 才是发布策略、稳定化、去重、打印和汇总统计的总控

你如果只记住一个结论，就记这个：

> 用户态 master 的核心，不是“读到一帧就打印”，而是“先把帧识别成候选，再把候选变成稳定事件，最后才允许发布”。
