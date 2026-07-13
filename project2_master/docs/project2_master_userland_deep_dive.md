# project2_master 用户态深读

这份文档只读 `project2_master/linux_app` 下的 RF 用户态链路：

- `main.c`
- `rf_source.c/h`
- `rf_epoll.c/h`
- `rf_decode.c/h`
- `rf_decode_c.c/h`
- `mqtt_publisher.c/h`

它对应的可执行文件是 `rf_gateway`。

兼容说明：以下先补回 `HEAD` 版章节骨架，便于沿用旧目录、旧引用和旧阅读顺序；后文现有正文、源码摘录和细讲全部保留。

## 0. 阅读路线图

兼容旧版目录：下文现有正文继续覆盖 `main -> rf_source -> rf_epoll -> rf_decode -> rf_decode_c -> JSON/MQTT/Qt` 主链，本轮新增代码块与细讲保持不删。

## 1. `main.c`：主循环不是循环本身，而是整条状态链

兼容旧版目录：对应下文现有 `## 1. 先记住这层的定位`、`## 2. 程序入口：main()`、`## 6. 事件策略层`、`## 7. stdout JSON envelope`、`## 8. 三种 payload 的职责`。

### 1.0 文件职责与函数索引

兼容旧版目录：对应当前正文里 `main.c`、`rf_source`、`rf_epoll`、`rf_decode`、`rf_decode_c`、`mqtt_publisher` 的分工说明。

### 1.1 运行时上下文

兼容旧版目录：对应当前正文里稳定分组、驱动序号、统计字段、MQTT 发布状态等运行时状态说明。

### 1.2 `main()` 的入口顺序

兼容旧版目录：对应下文现有 `## 2. 程序入口：main()`。

### 1.3 事件泵配置

兼容旧版目录：对应当前正文里 `rf_epoll` 回调配置、启动期状态刷新与 `device_status` 首发逻辑。

### 1.4 `on_rf_frame()`：真正的业务核心

兼容旧版目录：对应当前正文里解码、过滤、稳定化、去重、封装和发布主流程。

#### 1.4.1 先记驱动丢帧

兼容旧版目录：对应当前正文里 `drv_seq` 缺口统计和 `drv_drop` 语义。

#### 1.4.2 再解码

兼容旧版目录：对应当前正文里 `rf_decode_frame()` 和最近一次调用统计。

#### 1.4.3 低置信度先丢

兼容旧版目录：对应当前正文里 `min_publish_confidence` 过滤。

#### 1.4.4 稳定分组不是解码，而是后处理

兼容旧版目录：对应下文现有 `## 6. 事件策略层：稳定分组、近邻合并、重复抑制`。

#### 1.4.5 重复发布抑制

兼容旧版目录：对应当前正文里 `publish_gap`、`last_code`、`last_publish_seq` 的抑制规则。

#### 1.4.6 事件发布不是最后一步，发布后还要更新状态

兼容旧版目录：对应当前正文里 `rf_event` 发布与 `ctx` 状态回写。

### 1.5 周期/退出汇总：单行 JSON envelope，而不是旧文本 stdout

兼容旧版目录：对应下文现有 `## 7. stdout JSON envelope 是这层最重要的输出契约`、`## 8. 三种 payload 的职责`、`## 9. MQTT publish 是旁路，不是主线`。

### 1.6 退出清理顺序

兼容旧版目录：对应当前正文里 shutdown 阶段的 `rf_stats` / `device_status` 输出与 `rf_source_close()`。

## 2. `rf_source.c/h`：只做设备白名单和 fd 生命周期

兼容旧版目录：对应下文现有 `## 3. rf_source：只允许 /dev/rf433`。

### 2.1 代码

兼容旧版目录：对应当前正文里路径白名单、字符设备校验、打开和关闭逻辑。

## 3. `rf_epoll.c/h`：事件泵只负责“持续喂帧”

兼容旧版目录：对应下文现有 `## 4. rf_epoll：把定长驱动帧接成事件循环`。

### 3.1 配置结构

兼容旧版目录：对应当前正文里 `rf_epoll_config_t` 的 fd、回调、统计与用户指针。

### 3.2 `consume_frames()`：把驱动帧搬成用户帧

兼容旧版目录：对应当前正文里从 `struct rf433_frame` 到 `rf_frame_t` 的搬运逻辑。

### 3.3 `rf_epoll_run()`：一个 epoll 同时管理数据和统计

兼容旧版目录：对应当前正文里 `epoll_wait()`、`timerfd`、帧流与统计流的调度关系。

### 3.4 事件泵与回压时序

兼容旧版目录：对应当前正文里 `read -> consume -> decode -> publish -> wait` 的节奏说明。

## 4. `rf_decode.c/h`：适配层，不是算法层

兼容旧版目录：对应下文现有 `## 5. 解码层：rf_decode 与 rf_decode_c`。

### 4.1 代码

兼容旧版目录：对应当前正文里 `rf_decode_frame()`、标准化 packet 和运行统计说明。

### 4.2 统计联动

兼容旧版目录：对应当前正文里 runtime 统计、last-call 统计和上层读取方式。

### 4.3 异常路径与退出顺序

兼容旧版目录：对应当前正文里 `EINTR`、`EAGAIN`、`EOF`、真错误与统一收口顺序说明。

## 5. `rf_decode_c.c/h`：EV1527 识别本体

兼容旧版目录：对应下文现有 `### 5.2 rf_decode_c 的角色` 以及当前正文里算法主链补充。

### 5.1 先看数据流

兼容旧版目录：对应当前正文里两种起始极性、候选窗口和最佳结果选择总览。

### 5.2 `build_runs_from_frame()`：先把脉冲变成 run

兼容旧版目录：对应当前正文里脉宽转 run 序列的阶段说明。

### 5.3 `decode_best_from_runs()`：先结构，再时序，再评分

兼容旧版目录：对应当前正文里结构筛选、时钟估计、位级判定和置信度打分。

#### 5.3.1 先找合法窗口

兼容旧版目录：对应当前正文里同步头与候选起点选择。

#### 5.3.2 再检查位级结构

兼容旧版目录：对应当前正文里高低交替结构校验。

#### 5.3.3 再估计时钟

兼容旧版目录：对应当前正文里 `clk` 融合估计。

#### 5.3.4 再按位计算双假设误差

兼容旧版目录：对应当前正文里 bit `0/1` 双假设比较。

#### 5.3.5 最后不是“能解就行”，而是“分数够不够高”

兼容旧版目录：对应当前正文里综合置信度构造与门槛说明。

#### 5.3.6 选择最佳候选

兼容旧版目录：对应当前正文里 `raw_code/address20/button4/confidence` 等最终结果封装。

### 5.4 `rf_decode_stage_stats_t` 是两级门控的可观测点

兼容旧版目录：对应当前正文里结构门和时序门统计含义。

## 6. 这三层统计怎么联动

兼容旧版目录：对应下文现有 `## 8. 三种 payload 的职责`、`## 9. MQTT publish 是旁路，不是主线`、`## 10. Qt 是怎样接上这条链的` 里的观测面说明。

### 6.1 驱动统计

兼容旧版目录：对应当前正文里 `GET_STATS` / `GET_STATUS`、`device_status` 与 `rf_stats` 的驱动字段。

### 6.2 用户态业务统计

兼容旧版目录：对应当前正文里 `frames_total/decode_ok/low_conf_drop/stable_drop/dup_drop/published_events` 的分层语义。

### 6.3 IO / 解码统计也折叠在 `rf_stats.payload`

兼容旧版目录：对应当前正文里 `read_eintr/read_eagain/read_error/decode_c_*` 等字段说明。

## 7. 最后再把关系说死

兼容旧版目录：对应当前正文里 `rf_source`、`rf_epoll`、`rf_decode`、`rf_decode_c`、`main.c` 和 Qt/MQTT 的职责边界总结。

## 1. 先记住这层的定位

`rf_gateway` 不是驱动，也不是 Qt 页面。它是两者之间的策略层和协议转换层。

更具体地说，它做四件事：

1. 作为 `/dev/rf433` 的用户态消费者
2. 把 pulse frame 解码成 EV1527 语义结果
3. 生成统一的 `stdout JSON envelope`
4. 在 MQTT 已连接时并行发布同一份 payload

如果用一句更工程化的话概括：

`rf_gateway = /dev/rf433 reader + EV1527 decoder + JSON envelope publisher + optional MQTT publisher`

## 2. 程序入口：`main()`

主函数大致按下面顺序工作：

```text
parse args
  -> validate rf_input
  -> open /dev/rf433
  -> init MQTT publisher
  -> query driver state
  -> emit startup device_status
  -> run rf_epoll loop
  -> emit shutdown rf_stats/device_status
```

读 `main()` 时，你会发现它刻意没有碰串口细节。它关心的是策略参数：

- `stable_repeat`
- `stable_window`
- `stable_near_bits`
- `min_publish_confidence`
- `publish_gap`

这些都属于“上层如何决定要不要把一帧当成有效业务事件发出去”的策略，而不是底层采集逻辑。

## 3. `rf_source`：只允许 `/dev/rf433`

### 3.1 路径白名单

`rf_source.h` 把默认路径固定成：

```c
#define RF_SOURCE_PATH "/dev/rf433"
```

`rf_source_is_supported_path()` 和 `main()` 都要求 RF 输入路径必须是这个值。

所以当前 master 用户态并不是“可注入任意 RF 数据源”的通用程序，而是一个明确绑定 `/dev/rf433` 的板侧网关。

来源：`project2_master/linux_app/rf_source.c`，函数：`rf_source_is_supported_path()` / `rf_source_open()`，作用：把 RF 输入路径锁死在 `/dev/rf433`，并且要求它真的是字符设备。

```c
int rf_source_is_supported_path(const char *path) {
    if (path == NULL || path[0] == '\0') {
        return 1;
    }
    return (strcmp(path, RF_SOURCE_PATH) == 0) ? 1 : 0;
}

static int rf_source_is_char_device(const char *path) {
    struct stat st;

    if (path == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (stat(path, &st) != 0) {
        return -1;
    }
    return S_ISCHR(st.st_mode) ? 1 : 0;
}

int rf_source_open(const char *path) {
    const char *resolved = (path == NULL || path[0] == '\0') ? RF_SOURCE_PATH : path;

    if (!rf_source_is_supported_path(resolved)) {
        errno = EINVAL;
        return -1;
    }

    {
        const int is_chr = rf_source_is_char_device(resolved);
        if (is_chr < 0) {
            return -1;
        }
        if (is_chr == 0) {
            errno = ENOTTY;
            return -1;
        }
    }

    return open(resolved, O_RDONLY | O_NONBLOCK);
}
```

这段实现把“白名单”写得非常硬：

- 空路径允许，是因为它会回落到默认的 `RF_SOURCE_PATH`。
- 非 `/dev/rf433` 直接 `EINVAL`，不是 warn 后继续。
- 即使路径字符串对了，也必须通过 `stat()` 证明它是字符设备，普通文件同样会被拒绝。

### 3.2 打开时还要确认字符设备

`rf_source_open()` 不只是 `open()`，还会先用 `stat()` 检查目标是不是字符设备。通过后才以 `O_RDONLY | O_NONBLOCK` 打开。

这一步非常重要，因为它明确说明当前程序预期面对的是“驱动导出的设备节点”，不是普通文件、不是串口日志文件，也不是 PC 仿真输入。

## 4. `rf_epoll`：把定长驱动帧接成事件循环

`rf_epoll_run()` 的工作可以拆成两部分：

### 4.1 监听 `/dev/rf433`

当 `epoll` 报告 `rf_fd` 可读时，`consume_frames()` 会循环 `read()`：

- 每次读一个完整 `struct rf433_frame`
- 取出其中的 `pulse_count`
- 把 `pulse[]` 搬进 `rf_frame_t`
- 再把 `timestamp_ns` 和 `seq` 作为独立参数传给回调

注意这个转换很有层次感：

- `rf_frame_t` 只保留共享脉冲帧语义
- 驱动补充的 `timestamp_ns` / `seq` 作为附加元数据单独向上传递

来源：`project2_master/linux_app/rf_epoll.c`，函数：`consume_frames()`，作用：把驱动导出的 `struct rf433_frame` 搬成共享层 `rf_frame_t`，再把 `timestamp_ns/seq` 单独上推给回调。

```c
static int consume_frames(const rf_epoll_config_t *cfg) {
    struct rf433_frame drv_frame;
    rf_frame_t frame;
    ssize_t n;

    while (1) {
        n = read(cfg->rf_fd, &drv_frame, sizeof(drv_frame));
        if (n < 0) {
            if (errno == EINTR) {
                if (cfg->stats != NULL) {
                    cfg->stats->read_eintr++;
                }
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                if (cfg->stats != NULL) {
                    cfg->stats->read_eagain++;
                }
                return 0;
            }
            if (cfg->stats != NULL) {
                cfg->stats->read_error++;
            }
            fprintf(stderr, "[RF_IO] read failed: errno=%d (%s)\n", errno, strerror(errno));
            return -1;
        }
        if (n == 0) {
            if (cfg->stats != NULL) {
                cfg->stats->read_eof++;
            }
            fprintf(stderr, "[RF_IO] read returned EOF on rf_fd=%d\n", cfg->rf_fd);
            return -2;
        }
        if ((size_t)n < sizeof(drv_frame)) {
            if (cfg->stats != NULL) {
                cfg->stats->short_read++;
            }
            fprintf(stderr, "[RF_IO] short read from driver: %zd/%zu\n", n, sizeof(drv_frame));
            continue;
        }

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
}
```

这段代码很值得对着 driver ABI 一起看：

- `read()` 返回的是 `struct rf433_frame`，但用户态业务链立刻把它降成共享层 `rf_frame_t`。
- `timestamp_ns` 和 `seq` 没有被塞进 `rf_frame_t`，而是作为并列参数传给 `on_frame` 回调。
- `short_read` 被显式当成异常路径处理，说明这条链假设 driver ABI 必须是一帧一个完整 struct。

### 4.2 定时拉取驱动统计

`rf_epoll_run()` 还会创建 `timerfd`。当前 `main.c` 把统计间隔设为 5 秒，所以每 5 秒会调用一次 `on_drv_stats()`：

1. 通过 ioctl 刷新驱动统计和状态
2. 发出一条 `device_status`
3. 再发出一条 `rf_stats`

这就是为什么即使一段时间没有新的解码事件，Qt 和日志仍然能看到状态更新。

来源：`project2_master/linux_app/rf_epoll.c`，函数：`rf_epoll_run()`，作用：把 `/dev/rf433` 和 `timerfd` 编织进同一个 epoll 循环。

```c
int rf_epoll_run(const rf_epoll_config_t *cfg) {
    int epfd = -1;
    int timer_fd = -1;
    struct epoll_event ev;
    struct epoll_event events[8];
    int nfds = 0;
    int i = 0;
    int rc = 0;

    if (cfg == NULL || cfg->rf_fd < 0 || cfg->on_frame == NULL) {
        return -1;
    }

    epfd = epoll_create1(0);
    if (epfd < 0) {
        return -2;
    }

    memset(&ev, 0, sizeof(ev));
    ev.events = EPOLLIN;
    ev.data.fd = cfg->rf_fd;
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, cfg->rf_fd, &ev) != 0) {
        close(epfd);
        return -3;
    }

    if (cfg->on_stats != NULL && cfg->stats_interval_s > 0) {
        struct itimerspec its;
        timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
        if (timer_fd >= 0) {
            memset(&its, 0, sizeof(its));
            its.it_value.tv_sec = cfg->stats_interval_s;
            its.it_interval.tv_sec = cfg->stats_interval_s;
            timerfd_settime(timer_fd, 0, &its, NULL);
            memset(&ev, 0, sizeof(ev));
            ev.events = EPOLLIN;
            ev.data.fd = timer_fd;
            epoll_ctl(epfd, EPOLL_CTL_ADD, timer_fd, &ev);
        }
    }

    while (1) {
        nfds = epoll_wait(epfd, events, (int)(sizeof(events) / sizeof(events[0])), -1);
        if (nfds < 0) {
            if (errno == EINTR) {
                if (cfg->stats != NULL) {
                    cfg->stats->epoll_eintr++;
                }
                continue;
            }
            if (cfg->stats != NULL) {
                cfg->stats->epoll_error++;
            }
            fprintf(stderr, "[RF_IO] epoll_wait failed: errno=%d (%s)\n", errno, strerror(errno));
            rc = -4;
            goto out;
        }
        for (i = 0; i < nfds; ++i) {
            if (events[i].data.fd == cfg->rf_fd) {
                if ((events[i].events & (EPOLLERR | EPOLLHUP)) != 0u) {
                    fprintf(
                        stderr,
                        "[RF_IO] epoll event=0x%X on rf_fd=%d (ERR/HUP)\n",
                        (unsigned)events[i].events,
                        cfg->rf_fd
                    );
                }
                if ((events[i].events & (EPOLLIN | EPOLLERR | EPOLLHUP)) == 0u) {
                    continue;
                }
                rc = consume_frames(cfg);
                if (rc != 0) {
                    goto out;
                }
            } else if (timer_fd >= 0 && events[i].data.fd == timer_fd) {
                uint64_t expirations;
                (void)read(timer_fd, &expirations, sizeof(expirations));
                if (cfg->on_stats != NULL) {
                    cfg->on_stats(cfg->rf_fd, cfg->user);
                }
            }
        }
    }

out:
    if (timer_fd >= 0) {
        close(timer_fd);
    }
    close(epfd);
    return rc;
}
```

`rf_epoll_run()` 不是只盯 `rf_fd` 的简单双循环，它把“业务帧输入”和“统计心跳”放到了同一个调度点上，所以 `device_status/rf_stats/rf_event` 三种消息天然来自同一条主链。

## 5. 解码层：`rf_decode` 与 `rf_decode_c`

### 5.1 `rf_decode_frame()`

这一层是业务解码入口。当前实现只调了一个算法分支：

- `rf_decode_ev1527_c_with_stats()`

成功时，它会把结果转成：

- `addr`
- `key`
- `raw_code`
- `confidence`
- `source = "c"`

因此 `addr/key/conf` 的来源非常明确：它们是用户态 EV1527 解码结果，不是共享协议字段，更不是驱动字段。

来源：`project2_master/linux_app/rf_decode.c`，函数：`rf_decode_frame()`，作用：调用 C 版 EV1527 解码器，填充统一的用户态结果结构，并记录耗时统计。

```c
int rf_decode_frame(
    const rf_frame_t *frame,
    rf_decoded_packet_t *out
) {
    unsigned long long t0, t1;
    rf_decode_result_c_t c_result;
    int c_rc;

    if (frame == NULL || out == NULL) {
        return -1;
    }

    memset(out, 0, sizeof(*out));
    memset(&g_rf_last_call_stats, 0, sizeof(g_rf_last_call_stats));
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
        g_rf_last_call_stats.rc = 0;
        g_rf_last_call_stats.c_ok = 1;
        g_rf_decode_stats.c_accepts++;
        g_rf_decode_stats.c_accept_total_us += g_rf_last_call_stats.c_total_us;
        return 0;
    }

    g_rf_last_call_stats.rc = RF_DECODE_RC_NO_FRAME;
    return RF_DECODE_RC_NO_FRAME;
}
```

这里可以直接看出 `rf_gateway` 里的高层字段从哪里长出来：

- `addr` 是 `raw_code` 的十六进制格式化结果。
- `key` 来自 `button4`，不是驱动里已有字段。
- `source` 被显式写成 `"c"`，说明当前 decode 路由没有多后端协商，只有 C 实现这一条。

### 5.2 `rf_decode_c` 的角色

`rf_decode_c.c` 是真正的 EV1527 波形识别算法所在位置。就当前代码事实看：

- 它根据脉冲宽度推导时钟
- 识别同步区和数据区
- 评分并选出最佳候选
- 最终产出 `raw_code`、按钮位、置信度等结果

文档在这里不展开算法细节，只保留一个关键事实：

解码发生在用户态，而且只在 `rf_gateway` 里发生。

## 6. 事件策略层：稳定分组、近邻合并、重复抑制

`main.c` 在解码成功后不会立刻发布事件，而是继续做三层过滤。

来源：`project2_master/linux_app/main.c`，函数：`on_rf_frame()`，作用：接住一帧共享 pulse frame，完成掉帧统计、解码、低置信度过滤、稳定分组、重复抑制和最终发布。

```c
static int on_rf_frame(const rf_frame_t *frame, uint64_t timestamp_ns, uint32_t drv_seq, void *user) {
    app_ctx_t *ctx = (app_ctx_t *)user;
    rf_decoded_packet_t pkt;
    rf_decode_last_call_stats_t call_stats;
    char payload[JSON_PAYLOAD_CAPACITY];
    int rc = 0;

    if (ctx == NULL || frame == NULL) {
        return -1;
    }

    if (ctx->has_drv_seq && drv_seq > ctx->drv_seq_prev) {
        const uint32_t gap = drv_seq - ctx->drv_seq_prev - 1u;
        if (gap > 0u) {
            if (ctx->drv_drop > (0xFFFFFFFFu - gap)) {
                ctx->drv_drop = 0xFFFFFFFFu;
            } else {
                ctx->drv_drop += gap;
            }
        }
    }
    ctx->drv_seq_prev = drv_seq;
    ctx->has_drv_seq = 1;

    ctx->frames_total++;
    ctx->frame_seq++;
    rc = rf_decode_frame(frame, &pkt);
    memset(&call_stats, 0, sizeof(call_stats));
    rf_decode_get_last_call_stats(&call_stats);
    if (rc != 0) {
        if (rc == RF_DECODE_RC_NO_FRAME) {
            ctx->decode_no_frame++;
        } else {
            ctx->decode_err++;
        }
        return 0;
    }
    ctx->decode_ok++;

    if (pkt.confidence < ctx->min_publish_confidence) {
        ctx->low_conf_drop++;
        return 0;
    }

    if (ctx->stable_repeat > 1u) {
        int idx = -1;
        rf_stable_group_t *g = NULL;
        stable_groups_decay(ctx);
        idx = stable_group_find(ctx, pkt.raw_code);
        if (idx < 0) {
            idx = stable_group_alloc(ctx);
            if (idx >= 0) {
                stable_group_seed(
                    &ctx->stable_groups[idx],
                    pkt.raw_code,
                    pkt.confidence,
                    ctx->frame_seq
                );
            }
            ctx->stable_drop++;
            return 0;
        }
        g = &ctx->stable_groups[idx];
        stable_group_update(g, pkt.raw_code, pkt.confidence, ctx->frame_seq);
        if (g->hits < ctx->stable_repeat) {
            ctx->stable_drop++;
            return 0;
        }
        pkt.raw_code = g->best_code & 0xFFFFFFu;
        snprintf(pkt.addr, sizeof(pkt.addr), "0x%06X", pkt.raw_code & 0xFFFFFFu);
        snprintf(pkt.key, sizeof(pkt.key), "%u", (unsigned)(pkt.raw_code & 0x0Fu));
        if (g->best_conf > pkt.confidence) {
            pkt.confidence = g->best_conf;
        }
    }

    if (
        ctx->has_last_code &&
        pkt.raw_code == ctx->last_code &&
        (ctx->frame_seq - ctx->last_publish_seq) < (uint32_t)ctx->publish_gap
    ) {
        ctx->dup_drop++;
        return 0;
    }

    if (build_rf_event_payload(payload, sizeof(payload), ctx, frame, &pkt, &call_stats, timestamp_ns, drv_seq) != 0) {
        fprintf(stderr, "[RF_JSON] failed to assemble rf event payload\n");
        return 0;
    }

    (void)emit_protocol_message(ctx, "rf_event", MQTT_TOPIC_RF_EVENT, payload, 0);
    ctx->last_code = pkt.raw_code;
    ctx->has_last_code = 1;
    ctx->last_publish_seq = ctx->frame_seq;
    ctx->published++;
    return 0;
}
```

三层策略其实就直接写在这个函数里：

- `drv_seq` 差值先换算成 `drv_drop`，说明用户态还能反推 driver 队列是否跳号。
- `min_publish_confidence`、`stable_repeat`、`publish_gap` 不是抽象设计，而是这里真实执行的三道门。
- 真正对外发布前，代码先调用 `build_rf_event_payload()`，再走 `emit_protocol_message()`，所以 JSON 契约和 MQTT 旁路都是这一层生成的。

### 6.1 低置信度过滤

如果 `pkt.confidence < min_publish_confidence`，当前帧直接丢弃，记入 `low_conf_drop`。

### 6.2 稳定分组

如果 `stable_repeat > 1`，程序会把最近若干帧按 `raw_code` 做近邻分组：

- `stable_group_find()` 用 Hamming 距离匹配近邻
- `stable_group_seed()` 建组
- `stable_group_update()` 更新最佳码值和置信度

默认参数下，这意味着单帧解码成功并不一定立刻对外发布；需要一定重复确认后才会出事件。

### 6.3 重复抑制

即使稳定分组通过了，如果当前 `raw_code` 与上次已发布事件一致，且距离上次发布的帧间隔小于 `publish_gap`，也会被当成重复事件压掉。

这就是为什么最终看到的 `rf_event` 数量通常会小于驱动读到的总帧数。

## 7. `stdout JSON envelope` 是这层最重要的输出契约

### 7.1 根对象格式

`build_protocol_line()` 组装的根对象长这样：

```json
{
  "type": "rf_event",
  "topic": "argi/device/rk3568-001/rf/event",
  "mqtt_published": true,
  "payload": {"device_id":"rk3568-001","type":"rf_event"}
}
```

这四个根字段必须分清：

- `type`
  Qt 用它决定当前是哪种消息。
- `topic`
  当前代码里的实际 MQTT topic。
- `mqtt_published`
  这一次 publish 是否成功。
- `payload`
  真正的业务内容。

来源：`project2_master/linux_app/main.c`，函数：`build_protocol_line()`，作用：把具体 payload 包上一层统一根对象，形成给 Qt 和日志消费的 `stdout JSON envelope`。

```c
static int build_protocol_line(
    char *line,
    size_t line_capacity,
    const char *type,
    const char *subtopic,
    int mqtt_published,
    const char *payload_json
) {
    size_t offset = 0u;

    if (
        line == NULL ||
        type == NULL ||
        subtopic == NULL ||
        payload_json == NULL
    ) {
        return -1;
    }

    if (
        appendf(
            line,
            line_capacity,
            &offset,
            "{\"type\":\"%s\",\"topic\":\"%s/%s\",\"mqtt_published\":%s,\"payload\":%s}",
            type,
            MQTT_TOPIC_ROOT,
            subtopic,
            json_bool(mqtt_published),
            payload_json
        ) != 0
    ) {
        return -1;
    }

    return 0;
}
```

这里非常关键的一点是：根对象里的 `topic` 和 `mqtt_published` 本质上是“附加传输元信息”，真正的业务字段都还在 `payload` 里。

### 7.2 这不是纯 MQTT 协议

根对象存在的意义不是“给 broker 看”，而是“给本地消费者和日志看”。`rf_gateway` 会先生成这条 JSON，再决定是否旁路发 MQTT。

所以：

- Qt 读的是这条本地 JSON 行
- MQTT 只是这条 payload 的额外输出
- 即使 MQTT 没连上，Qt 仍可以继续工作

## 8. 三种 payload 的职责

### 8.1 `device_status`

这类消息描述网关当前状态和驱动摘要，例如：

- `rf_input`
- `rf_online`
- `mqtt_connected`
- 驱动统计摘要
- 稳定分组参数
- `reason`（如 `startup`、`driver_stats`、`shutdown`）

它更像“状态心跳”。

### 8.2 `rf_stats`

这类消息偏统计：

- `frames_total`
- `decode_ok`
- `decode_no_frame`
- `decode_err`
- `low_conf_drop`
- `stable_drop`
- `dup_drop`
- `drv_drop`
- driver/io 路径统计

它更像“调优和观测面”。

### 8.3 `rf_event`

这类消息才是 Qt RF 页面最关心的业务事件。它包含：

- `addr`
- `key`
- `conf` / `confidence`
- `src` / `source`
- `seq`
- `drv_seq`
- `timestamp_ns`
- `decode_us`
- `pulse_count`
- `pulse_us[]`

其中最关键的是 `pulse_us[]`。Qt 波形预览直接用的就是这个数组，而不是 UI 侧重新推断出的假波形。

来源：`project2_master/linux_app/main.c`，函数：`build_rf_event_payload()`，作用：把 decode 结果、driver 元数据和原始脉冲数组打进 `rf_event.payload`。

```c
static int build_rf_event_payload(
    char *payload,
    size_t capacity,
    const app_ctx_t *ctx,
    const rf_frame_t *frame,
    const rf_decoded_packet_t *pkt,
    const rf_decode_last_call_stats_t *call_stats,
    uint64_t timestamp_ns,
    uint32_t drv_seq
) {
    size_t offset = 0u;
    uint16_t i = 0u;

    if (
        payload == NULL ||
        ctx == NULL ||
        frame == NULL ||
        pkt == NULL ||
        call_stats == NULL
    ) {
        return -1;
    }
    if (
        appendf(
            payload,
            capacity,
            &offset,
            "{"
            "\"device_id\":\"%s\","
            "\"type\":\"rf_event\","
            "\"rf_input\":\"%s\","
            "\"addr\":\"%s\","
            "\"key\":\"%s\","
            "\"conf\":%.4f,"
            "\"confidence\":%.4f,"
            "\"src\":\"%s\","
            "\"source\":\"%s\","
            "\"seq\":%u,"
            "\"drv_seq\":%u,"
            "\"timestamp_ns\":%llu,"
            "\"decode_us\":%llu,"
            "\"mqtt_connected\":%s,"
            "\"pulse_count\":%u,"
            "\"pulse_us\":[",
            MQTT_DEVICE_ID,
            ctx->rf_input,
            pkt->addr,
            pkt->key,
            pkt->confidence,
            pkt->confidence,
            pkt->source,
            pkt->source,
            (unsigned)ctx->frame_seq,
            (unsigned)drv_seq,
            (unsigned long long)timestamp_ns,
            (unsigned long long)call_stats->total_us,
            json_bool(mqtt_publisher_is_connected(&ctx->mqtt)),
            (unsigned)frame->len
        ) != 0
    ) {
        return -1;
    }

    for (i = 0u; i < frame->len; ++i) {
        if (appendf(payload, capacity, &offset, "%s%u", (i == 0u) ? "" : ",", (unsigned)frame->pulse[i]) != 0) {
            return -1;
        }
    }

    if (appendf(payload, capacity, &offset, "]}") != 0) {
        return -1;
    }
    return 0;
}
```

这段代码把 `rf_event` 的字段来源讲死了：

- `addr/key/conf/source` 来自 decode 结果 `pkt`。
- `drv_seq/timestamp_ns` 来自 driver 帧外壳。
- `seq/decode_us` 来自用户态自己的处理过程。
- `pulse_us[]` 则是共享 pulse frame 原样展开后的最后一份“真值数组”。

## 9. MQTT publish 是旁路，不是主线

`emit_protocol_message()` 的顺序很值得单独拎出来：

1. 如果 MQTT 已连接，先尝试 publish payload
2. 无论 publish 成功与否，都组装 JSON envelope
3. 把 JSON 行写到 `stdout`
4. 如果 publish 失败，再把错误写到 `stderr`

这代表了当前实现的明确优先级：

- 本地 `stdout` 协议是 RF 主线
- MQTT 是可选的外发旁支

而且当前代码里只有 publish，没有 subscribe，因此不能把 MQTT command 写成已实现能力。

来源：`project2_master/linux_app/main.c`，函数：`emit_protocol_message()`，作用：先尝试 MQTT publish，再无条件把同一份 envelope 写到 `stdout`。

```c
static int emit_protocol_message(
    app_ctx_t *ctx,
    const char *type,
    const char *subtopic,
    const char *payload_json,
    int retain
) {
    char line[JSON_LINE_CAPACITY];
    int publish_rc = MQTT_PUBLISHER_ERR_NO_CONN;
    int mqtt_published = 0;

    if (ctx == NULL || type == NULL || subtopic == NULL || payload_json == NULL) {
        return -1;
    }

    if (mqtt_publisher_is_connected(&ctx->mqtt)) {
        publish_rc = mqtt_publisher_publish(&ctx->mqtt, subtopic, payload_json, retain);
        mqtt_published = (publish_rc == 0);
    }

    if (build_protocol_line(line, sizeof(line), type, subtopic, mqtt_published, payload_json) != 0) {
        fprintf(stderr, "[RF_JSON] failed to assemble protocol line for %s\n", type);
        return -1;
    }

    fprintf(stdout, "%s\n", line);

    if (publish_rc != 0 && publish_rc != MQTT_PUBLISHER_ERR_NO_CONN) {
        fprintf(
            stderr,
            "[MQTT] publish %s/%s failed: %s\n",
            MQTT_TOPIC_ROOT,
            subtopic,
            mqtt_publisher_error_string(publish_rc)
        );
    }

    return mqtt_published ? 0 : publish_rc;
}
```

这段顺序直接说明了优先级：

- `stdout` 行一定会写，只要 `build_protocol_line()` 成功。
- MQTT 失败不会阻断 Qt 消费主链，只会额外落一条错误到 `stderr`。
- `mqtt_published` 不是配置项，而是这一次 publish 的实际结果位。

## 10. Qt 是怎样接上这条链的

虽然 Qt 源码在别的目录，这里还是要把关系讲透。

`qt_gui/rf/RFGatewayClient` 会：

1. 在应用目录找固定路径 `rf_gateway`
2. 用 `--rf-input /dev/rf433` 拉起它
3. 监听 `stdout`
4. 对每一行调用 `parseProtocolEnvelope()`
5. 按 `type` 分流：
   - `rf_event` -> 刷新 RF 波形、最近解码和历史表
   - `device_status` -> 刷新在线状态和部分计数
   - `rf_stats` -> 刷新错误与掉帧统计
6. 如果 `mqtt_published` 为真，再额外写一条 MQTT 日志

这条关系必须清楚写出来：

- `rf_gateway stdout` 是 Qt RF 页面输入
- `mqtt_published` 只决定是否在日志里多记一次 MQTT 发布
- RF 页面不是通过 MQTT 订阅刷新

来源：`project2_master/qt_gui/rf/rf_gateway_client.cpp`，函数：`parseProtocolEnvelope()`，作用：把 `rf_gateway` 输出的一整行 JSON 拆成 `type/topic/mqtt_published/payload` 四部分。

```cpp
bool RFGatewayClient::parseProtocolEnvelope(
    const QString &line,
    QString *type,
    QString *topic,
    bool *mqttPublished,
    QJsonObject *payload
) const {
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(line.toUtf8(), &parseError);
    const QJsonObject root = doc.object();

    if (type == nullptr || topic == nullptr || mqttPublished == nullptr || payload == nullptr) {
        return false;
    }
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        return false;
    }

    *type = scalarJsonString(root.value(QStringLiteral("type")));
    *topic = scalarJsonString(root.value(QStringLiteral("topic")));
    *mqttPublished = root.value(QStringLiteral("mqtt_published")).toBool(false);
    if (type->isEmpty() || !root.value(QStringLiteral("payload")).isObject()) {
        return false;
    }

    *payload = root.value(QStringLiteral("payload")).toObject();
    return true;
}
```

这段代码与 `build_protocol_line()` 是一一对称的：Linux 侧怎样包，Qt 侧就怎样拆，没有中间魔法层。

来源：`project2_master/qt_gui/rf/rf_gateway_client.cpp`，函数：`parseRFEventPayload()`，作用：从 `payload` 中抽出 `addr/key/conf/src/pulse_us[]`，还原成 Qt 侧 `RFEvent` 和脉冲数组。

```cpp
bool RFGatewayClient::parseRFEventPayload(const QJsonObject &payload, RFEvent *event, QVector<int> *pulses) const {
    const QString address = scalarJsonString(payload.value(QStringLiteral("addr")));
    const QString key = scalarJsonString(payload.value(QStringLiteral("key")));
    const QString source = scalarJsonString(payload.value(QStringLiteral("src")));
    const QJsonValue confValue = payload.value(QStringLiteral("conf"));
    const QJsonValue pulseArrayValue = payload.value(QStringLiteral("pulse_us"));
    const QJsonArray pulseArray = pulseArrayValue.toArray();

    if (event == nullptr || pulses == nullptr) {
        return false;
    }
    if (address.isEmpty() || key.isEmpty() || source.isEmpty() || !confValue.isDouble() || !pulseArrayValue.isArray()) {
        return false;
    }

    event->timestamp = QDateTime::currentDateTime();
    event->address = address;
    event->key = key;
    event->confidence = confValue.toDouble();
    event->source = source;
    event->frameSeq = payload.value(QStringLiteral("seq")).isDouble()
        ? static_cast<qint64>(payload.value(QStringLiteral("seq")).toDouble(-1.0))
        : -1;
    event->decodeUs = payload.value(QStringLiteral("decode_us")).isDouble()
        ? static_cast<qint64>(payload.value(QStringLiteral("decode_us")).toDouble(-1.0))
        : -1;

    pulses->clear();
    pulses->reserve(pulseArray.size());
    for (const QJsonValue &value : pulseArray) {
        const int pulseUs = value.toInt(-1);
        if (!value.isDouble() || pulseUs <= 0) {
            pulses->clear();
            return false;
        }
        pulses->append(pulseUs);
    }

    return true;
}
```

这就是为什么前面文档要强调 `pulse_us[]`：Qt 不是自己从 `addr/key` 反推真波形，而是直接消费 payload 里的真实脉冲数组。

来源：`project2_master/qt_gui/rf/rf_gateway_client.cpp`，函数：`handleProtocolLine()`，作用：按 `type` 把 envelope 分流到 RF 事件、设备状态和统计更新。

```cpp
void RFGatewayClient::handleProtocolLine(const QString &line) {
    QString type;
    QString topic;
    QJsonObject payload;
    bool mqttPublished = false;

    if (backend_ == nullptr) {
        return;
    }

    if (!parseProtocolEnvelope(line, &type, &topic, &mqttPublished, &payload)) {
        backend_->incrementParseError();
        backend_->addLog("WARN", "RF", QString("Invalid rf_gateway protocol JSON: %1").arg(line));
        return;
    }

    if (type == QStringLiteral("rf_event")) {
        RFEvent event;
        QVector<int> pulses;
        if (!parseRFEventPayload(payload, &event, &pulses)) {
            backend_->incrementParseError();
            backend_->addLog("WARN", "RF", QString("Invalid rf_event payload: %1").arg(line));
            return;
        }

        backend_->updateSerialStatus(true, scalarJsonString(payload.value(QStringLiteral("rf_input"))).isEmpty()
            ? resolvedRfInputPath()
            : scalarJsonString(payload.value(QStringLiteral("rf_input"))));
        backend_->addRFEvent(event, pulses);
        backend_->addLog("INFO", "RF", line);
        if (mqttPublished && !topic.isEmpty()) {
            backend_->addMqttPublishLog(topic, QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact)));
        }
        return;
    }

    if (type == QStringLiteral("device_status")) {
        const QString rfInputPath = scalarJsonString(payload.value(QStringLiteral("rf_input")));
        const bool rfOnline = payload.value(QStringLiteral("rf_online")).toBool(false);
        const int crcErrors = payload.value(QStringLiteral("driver_crc_err")).toInt(-1);
        const int driverDropFrames = payload.value(QStringLiteral("app_drv_drop")).toInt(-1);
        backend_->updateSerialStatus(rfOnline, rfInputPath.isEmpty() ? resolvedRfInputPath() : rfInputPath);
        backend_->updateProtocolStats(crcErrors, -1, driverDropFrames);
        backend_->addLog("INFO", "RF", line);
        if (mqttPublished && !topic.isEmpty()) {
            backend_->addMqttPublishLog(topic, QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact)));
        }
        return;
    }

    if (type == QStringLiteral("rf_stats")) {
        const int crcErrors = payload.value(QStringLiteral("driver_crc_err")).toInt(-1);
        const int parseErrors =
            payload.value(QStringLiteral("decode_no_frame")).toInt(0) +
            payload.value(QStringLiteral("decode_err")).toInt(0);
        const int driverDropFrames = payload.value(QStringLiteral("drv_drop")).toInt(-1);
        if (payload.contains(QStringLiteral("driver_online"))) {
            backend_->updateSerialStatus(
                payload.value(QStringLiteral("driver_online")).toBool(false),
                scalarJsonString(payload.value(QStringLiteral("rf_input"))).isEmpty()
                    ? resolvedRfInputPath()
                    : scalarJsonString(payload.value(QStringLiteral("rf_input")))
            );
        }
        backend_->updateProtocolStats(crcErrors, parseErrors, driverDropFrames);
        backend_->addLog("INFO", "RF", line);
        if (mqttPublished && !topic.isEmpty()) {
            backend_->addMqttPublishLog(topic, QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact)));
        }
        return;
    }

    backend_->incrementParseError();
    backend_->addLog("WARN", "RF", QString("Unknown rf_gateway protocol type: %1").arg(line));
}
```

Qt 侧真正依赖的是 `type + payload` 这组本地协议，不是 MQTT 订阅结果。`mqtt_published` 只是在本地日志上多挂一个“这次也顺手发出去了”的标记。

## 11. 当前没有实现什么

这一节专门防止文档越界。

### 11.1 没有 MQTT command

当前 `mqtt_publisher.c` 只有连接和发布，没有订阅、没有回调、没有命令执行。

### 11.2 没有 GPIO 输入

用户态 RF 主链只接受 `/dev/rf433`，没有 GPIO 事件源接入逻辑。

### 11.3 没有事件录像闭环

`rf_gateway` 当前只负责 RF 帧、解码、JSON 和 MQTT publish，看不到录像调度或 `record_done` 输出路径。

## 12. 推荐的源码走读顺序

1. `main.c`
   先看整条业务策略主干。
2. `rf_source.c`
   看为什么输入路径被固定死在 `/dev/rf433`。
3. `rf_epoll.c`
   看驱动帧如何进入事件循环。
4. `rf_decode.c`
   看 pulse frame 如何转成统一解码结果。
5. `rf_decode_c.c`
   看 EV1527 算法主体。
6. `mqtt_publisher.c`
   最后补齐 publish 这条旁路。
