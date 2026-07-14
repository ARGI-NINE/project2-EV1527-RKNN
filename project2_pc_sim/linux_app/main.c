#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rf_decode.h"
#include "rf_epoll.h"
#include "rf_source.h"

#define RF_STABLE_GROUP_MAX 12u

typedef struct {
    int active;
    uint32_t anchor_code;
    uint32_t best_code;
    float best_conf;
    uint32_t hits;
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
} app_ctx_t;

static void print_usage(const char *exe) {
    printf("Usage: %s [options]\n", exe);
    printf("  --rf-input -              Replay pulse stream from stdin (default -)\n");
    printf("  --stable-repeat <N>       Need N agreeing frames (default 2)\n");
    printf("  --stable-window <N>       Group memory window in frames (default 12)\n");
    printf("  --stable-near-bits <N>    Hamming-near threshold for merge (default 4)\n");
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

static void stable_group_seed(rf_stable_group_t *g, uint32_t code, float conf, uint32_t seq) {
    memset(g, 0, sizeof(*g));
    g->active = 1;
    g->anchor_code = code & 0xFFFFFFu;
    g->best_code = code & 0xFFFFFFu;
    g->best_conf = conf;
    g->hits = 1u;
    g->last_seq = seq;
}

static void stable_group_update(rf_stable_group_t *g, uint32_t code, float conf, uint32_t seq) {
    if (g->hits < UINT32_MAX) {
        g->hits++;
    }
    g->last_seq = seq;
    if (conf >= g->best_conf) {
        g->best_conf = conf;
        g->best_code = code & 0xFFFFFFu;
    }
}

static int on_rf_frame(const rf_frame_t *frame, void *user) {
    app_ctx_t *ctx = (app_ctx_t *)user;
    rf_decoded_packet_t pkt;
    int rc = 0;

    if (ctx == NULL || frame == NULL) {
        return -1;
    }

    ctx->frames_total++;
    ctx->frame_seq++;
    rc = rf_decode_frame(frame, &pkt);
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

    printf(
        "{\"addr\":\"%s\",\"key\":\"%s\",\"conf\":%.2f,\"src\":\"%s\",\"pulses\":%u,\"seq\":%u}\n",
        pkt.addr,
        pkt.key,
        pkt.confidence,
        pkt.source,
        frame->len,
        (unsigned)ctx->frame_seq
    );
    ctx->last_code = pkt.raw_code;
    ctx->has_last_code = 1;
    ctx->last_publish_seq = ctx->frame_seq;
    ctx->published++;
    return 0;
}

int main(int argc, char **argv) {
    const char *rf_input = "-";
    uint16_t stable_repeat = 2u;
    uint16_t stable_window = 12u;
    uint8_t stable_near_bits = 4u;
    float min_publish_conf = 0.72f;
    uint16_t publish_gap = 6u;
    app_ctx_t ctx;
    int rf_fd = -1;
    int i = 0;
    rf_epoll_config_t cfg;

    /* Ensure logs are flushed immediately when rf_gateway is piped by Qt. */
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    memset(&ctx, 0, sizeof(ctx));
    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--rf-input") == 0 && i + 1 < argc) {
            rf_input = argv[++i];
            if (!(rf_input[0] == '-' && rf_input[1] == '\0')) {
                fprintf(stderr, "--rf-input only supports '-' in pc_sim replay mode.\n");
                return 1;
            }
        } else if (strcmp(argv[i], "--stable-repeat") == 0 && i + 1 < argc) {
            stable_repeat = (uint16_t)strtoul(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--stable-window") == 0 && i + 1 < argc) {
            stable_window = (uint16_t)strtoul(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--stable-near-bits") == 0 && i + 1 < argc) {
            stable_near_bits = (uint8_t)strtoul(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--min-publish-confidence") == 0 && i + 1 < argc) {
            min_publish_conf = (float)atof(argv[++i]);
        } else if (strcmp(argv[i], "--publish-gap") == 0 && i + 1 < argc) {
            publish_gap = (uint16_t)strtoul(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "Unknown arg: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    rf_fd = rf_source_open(rf_input);
    if (rf_fd < 0) {
        fprintf(stderr, "Open RF input failed: %s\n", rf_input);
        return 2;
    }

    ctx.publish_gap = publish_gap;
    ctx.stable_repeat = (stable_repeat > 0u) ? stable_repeat : 1u;
    ctx.stable_window = (stable_window > 0u) ? stable_window : 12u;
    if (stable_near_bits > 12u) {
        stable_near_bits = 12u;
    }
    ctx.stable_near_bits = stable_near_bits;
    ctx.min_publish_confidence = min_publish_conf;

    cfg.rf_fd = rf_fd;
    cfg.rewind_on_eof = 0;
    cfg.on_frame = on_rf_frame;
    cfg.user = &ctx;

    (void)rf_epoll_run(&cfg);

    rf_source_close(rf_fd);
    return 0;
}
