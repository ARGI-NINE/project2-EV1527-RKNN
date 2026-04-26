#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/ioctl.h>
#include <sys/time.h>

#include "rf_decode.h"
#include "rf_epoll.h"
#include "rf_source.h"
#include "rf433_ioctl.h"

#define RF_STABLE_GROUP_MAX 12u

typedef struct {
    int active;
    uint32_t anchor_code;
    uint32_t best_code;
    float best_conf;
    uint16_t hits;
    uint16_t target_hits;
    uint32_t last_seq;
} rf_stable_group_t;

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

static void print_usage(const char *exe) {
    printf("Usage: %s [options]\n", exe);
    printf("  --rf-input <path>         Device path (default: %s)\n", RF_SOURCE_PATH);
    printf("  --stable-repeat <N>       Need N agreeing frames (default 2)\n");
    printf("  --stable-window <N>       Group memory window in frames (default 12)\n");
    printf("  --stable-near-bits <N>    Hamming-near threshold for merge (default 4)\n");
    printf("  --preferred-code <hex>    Prefer this code when seen in merged group (default 0x12D1B1)\n");
    printf("  --min-publish-confidence <F>  Suppress low-score decoded output (default 0.72)\n");
    printf("  --publish-gap <N>         Min frame gap for same code re-publish (default 6)\n");
}

static uint8_t hamming24(uint32_t a, uint32_t b) {
    uint32_t x = (a ^ b) & 0xFFFFFFu;
    uint8_t n = 0u;
    while (x != 0u) {
        n = (uint8_t)(n + (uint8_t)(x & 1u));
        x >>= 1u;
    }
    return n;
}

static void stable_groups_decay(app_ctx_t *ctx) {
    uint16_t i = 0u;
    if (ctx == NULL) {
        return;
    }
    for (i = 0u; i < RF_STABLE_GROUP_MAX; ++i) {
        rf_stable_group_t *g = &ctx->stable_groups[i];
        if (!g->active) {
            continue;
        }
        if ((ctx->frame_seq - g->last_seq) > (uint32_t)ctx->stable_window) {
            memset(g, 0, sizeof(*g));
        }
    }
}

static int stable_group_find(const app_ctx_t *ctx, uint32_t code) {
    int best_idx = -1;
    uint8_t best_dist = 255u;
    uint16_t i = 0u;
    if (ctx == NULL) {
        return -1;
    }
    for (i = 0u; i < RF_STABLE_GROUP_MAX; ++i) {
        const rf_stable_group_t *g = &ctx->stable_groups[i];
        uint8_t dist = 0u;
        if (!g->active) {
            continue;
        }
        dist = hamming24(code, g->anchor_code);
        if (dist > ctx->stable_near_bits) {
            continue;
        }
        if (best_idx < 0 || dist < best_dist) {
            best_idx = (int)i;
            best_dist = dist;
        }
    }
    return best_idx;
}

static int stable_group_alloc(app_ctx_t *ctx) {
    int best_idx = 0;
    uint32_t oldest_seq = 0xFFFFFFFFu;
    uint16_t i = 0u;
    if (ctx == NULL) {
        return -1;
    }
    for (i = 0u; i < RF_STABLE_GROUP_MAX; ++i) {
        if (!ctx->stable_groups[i].active) {
            return (int)i;
        }
    }
    for (i = 0u; i < RF_STABLE_GROUP_MAX; ++i) {
        if (ctx->stable_groups[i].last_seq < oldest_seq) {
            oldest_seq = ctx->stable_groups[i].last_seq;
            best_idx = (int)i;
        }
    }
    return best_idx;
}

static void stable_group_seed(rf_stable_group_t *g, uint32_t code, float conf, uint32_t seq, uint32_t preferred) {
    if (g == NULL) {
        return;
    }
    memset(g, 0, sizeof(*g));
    g->active = 1;
    g->anchor_code = code & 0xFFFFFFu;
    g->best_code = code & 0xFFFFFFu;
    g->best_conf = conf;
    g->hits = 1u;
    g->target_hits = ((code & 0xFFFFFFu) == (preferred & 0xFFFFFFu)) ? 1u : 0u;
    g->last_seq = seq;
}

static void stable_group_update(rf_stable_group_t *g, uint32_t code, float conf, uint32_t seq, uint32_t preferred) {
    if (g == NULL) {
        return;
    }
    g->hits++;
    g->last_seq = seq;
    if (conf >= g->best_conf) {
        g->best_conf = conf;
        g->best_code = code & 0xFFFFFFu;
    }
    if ((code & 0xFFFFFFu) == (preferred & 0xFFFFFFu)) {
        g->target_hits++;
    }
}

static int on_rf_frame(const rf_frame_t *frame, uint64_t timestamp_ns, uint32_t drv_seq, void *user) {
    app_ctx_t *ctx = (app_ctx_t *)user;
    rf_decoded_packet_t pkt;
    rf_decode_last_call_stats_t call_stats;
    int rc = 0;
    (void)timestamp_ns;

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
                    ctx->frame_seq,
                    ctx->preferred_code
                );
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

    printf(
        "[RF] addr=%s key=%s conf=%.2f source=%s pulses=%u seq=%u decode_us=%llu pulse_us=",
        pkt.addr,
        pkt.key,
        pkt.confidence,
        pkt.source,
        frame->len,
        (unsigned)ctx->frame_seq,
        call_stats.total_us
    );
    for (uint16_t i = 0; i < frame->len; ++i) {
        printf("%s%u", (i == 0u) ? "" : ",", (unsigned)frame->pulse[i]);
    }
    printf("\n");
    ctx->last_code = pkt.raw_code;
    ctx->has_last_code = 1;
    ctx->last_publish_seq = ctx->frame_seq;
    ctx->published++;
    return 0;
}

static void on_drv_stats(int rf_fd, void *user) {
    struct rf433_stats drv_stats;
    struct rf433_status drv_status;
    (void)user;

    if (ioctl(rf_fd, RF433_IOC_GET_STATS, &drv_stats) == 0 &&
        ioctl(rf_fd, RF433_IOC_GET_STATUS, &drv_status) == 0) {
        printf(
            "[DRV_STATS] frame_ok=%llu crc_err=%llu len_err=%llu drop=%llu"
            " online=%u seq=%u queue=%u/%u\n",
            (unsigned long long)drv_stats.frame_ok,
            (unsigned long long)drv_stats.crc_err,
            (unsigned long long)drv_stats.len_err,
            (unsigned long long)drv_stats.drop_cnt,
            (unsigned)drv_status.online,
            (unsigned)drv_status.seq,
            (unsigned)drv_status.queue_depth,
            (unsigned)drv_status.queue_capacity
        );
    }
}

int main(int argc, char **argv) {
    const char *rf_input = RF_SOURCE_PATH;
    uint16_t stable_repeat = 2u;
    uint16_t stable_window = 12u;
    uint8_t stable_near_bits = 4u;
    uint32_t preferred_code = 0x12D1B1u;
    float min_publish_conf = 0.72f;
    uint16_t publish_gap = 6u;
    app_ctx_t ctx;
    int rf_fd = -1;
    int i = 0;
    rf_epoll_config_t cfg;
    rf_epoll_stats_t epoll_stats;
    int epoll_rc = 0;
    rf_decode_runtime_stats_t decode_stats;

    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    memset(&ctx, 0, sizeof(ctx));
    memset(&epoll_stats, 0, sizeof(epoll_stats));
    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--rf-input") == 0 && i + 1 < argc) {
            rf_input = argv[++i];
        } else if (strcmp(argv[i], "--stable-repeat") == 0 && i + 1 < argc) {
            stable_repeat = (uint16_t)strtoul(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--stable-window") == 0 && i + 1 < argc) {
            stable_window = (uint16_t)strtoul(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--stable-near-bits") == 0 && i + 1 < argc) {
            stable_near_bits = (uint8_t)strtoul(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--preferred-code") == 0 && i + 1 < argc) {
            preferred_code = (uint32_t)strtoul(argv[++i], NULL, 0);
            preferred_code &= 0xFFFFFFu;
        } else if (strcmp(argv[i], "--min-publish-confidence") == 0 && i + 1 < argc) {
            min_publish_conf = (float)atof(argv[++i]);
        } else if (strcmp(argv[i], "--publish-gap") == 0 && i + 1 < argc) {
            publish_gap = (uint16_t)strtoul(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else {
            printf("Unknown arg: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
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

    ctx.publish_gap = publish_gap;
    ctx.stable_repeat = (stable_repeat > 0u) ? stable_repeat : 1u;
    ctx.stable_window = (stable_window > 0u) ? stable_window : 12u;
    if (stable_near_bits > 12u) {
        stable_near_bits = 12u;
    }
    ctx.stable_near_bits = stable_near_bits;
    ctx.preferred_code = preferred_code & 0xFFFFFFu;
    ctx.min_publish_confidence = min_publish_conf;

    cfg.rf_fd = rf_fd;
    cfg.on_frame = on_rf_frame;
    cfg.on_stats = on_drv_stats;
    cfg.stats_interval_s = 5;
    cfg.stats = &epoll_stats;
    cfg.user = &ctx;

    printf(
        "rf_gateway running: rf=%s stable=%u near=%u stable_win=%u preferred=0x%06X pub_conf=%.2f gap=%u\n",
        rf_input,
        (unsigned)stable_repeat,
        (unsigned)ctx.stable_near_bits,
        (unsigned)ctx.stable_window,
        (unsigned)ctx.preferred_code,
        min_publish_conf,
        (unsigned)publish_gap
    );
    epoll_rc = rf_epoll_run(&cfg);
    if (epoll_rc != 0) {
        fprintf(stderr, "[RF_IO] rf_epoll_run exited with rc=%d\n", epoll_rc);
    }

    printf(
        "[RF_STATS] frames_total=%u decode_ok=%u decode_no_frame=%u decode_err=%u"
        " low_conf_drop=%u stable_drop=%u dup_drop=%u published=%u drv_drop=%u\n",
        (unsigned)ctx.frames_total,
        (unsigned)ctx.decode_ok,
        (unsigned)ctx.decode_no_frame,
        (unsigned)ctx.decode_err,
        (unsigned)ctx.low_conf_drop,
        (unsigned)ctx.stable_drop,
        (unsigned)ctx.dup_drop,
        (unsigned)ctx.published,
        (unsigned)ctx.drv_drop
    );
    printf(
        "[RF_IO_STATS] read_eintr=%u read_eagain=%u read_eof=%u read_error=%u epoll_eintr=%u epoll_error=%u\n",
        (unsigned)epoll_stats.read_eintr,
        (unsigned)epoll_stats.read_eagain,
        (unsigned)epoll_stats.read_eof,
        (unsigned)epoll_stats.read_error,
        (unsigned)epoll_stats.epoll_eintr,
        (unsigned)epoll_stats.epoll_error
    );
    memset(&decode_stats, 0, sizeof(decode_stats));
    rf_decode_get_runtime_stats(&decode_stats);
    printf(
        "[RF_DECODE_STATS] c_attempts=%u c_accepts=%u c_total_us=%llu c_accept_total_us=%llu\n",
        decode_stats.c_attempts,
        decode_stats.c_accepts,
        decode_stats.c_total_us,
        decode_stats.c_accept_total_us
    );

    rf_source_close(rf_fd);
    return 0;
}
