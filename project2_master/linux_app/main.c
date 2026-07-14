#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>

#include "mqtt_publisher.h"
#include "rf_decode.h"
#include "rf_epoll.h"
#include "rf_source.h"
#include "rf433_ioctl.h"

#define RF_STABLE_GROUP_MAX 12u
#define MQTT_BROKER_HOST "192.168.30.26"
#define MQTT_BROKER_PORT 1883
#define MQTT_DEVICE_ID "rk3568-001"
#define MQTT_TOPIC_ROOT "argi/device/rk3568-001"
#define MQTT_TOPIC_STATUS "status"
#define MQTT_TOPIC_RF_EVENT "rf/event"
#define MQTT_TOPIC_RF_STATS "rf/stats"
#define JSON_PAYLOAD_CAPACITY 24576u
#define JSON_LINE_CAPACITY 28672u

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
    uint32_t drv_seq_prev;
    uint32_t drv_drop;
    int has_drv_seq;
    const char *rf_input;
    mqtt_publisher_t mqtt;
    struct rf433_stats drv_stats;
    struct rf433_status drv_status;
    int has_drv_stats;
    rf_epoll_stats_t *epoll_stats;
} app_ctx_t;

static void print_usage(FILE *stream, const char *exe) {
    fprintf(stream, "Usage: %s [options]\n", exe);
    fprintf(stream, "  --rf-input <path>         Device path (default: %s)\n", RF_SOURCE_PATH);
    fprintf(stream, "  --stable-repeat <N>       Need N agreeing frames (default 2)\n");
    fprintf(stream, "  --stable-window <N>       Group memory window in frames (default 12)\n");
    fprintf(stream, "  --stable-near-bits <N>    Hamming-near threshold for merge (default 4)\n");
    fprintf(stream, "  --min-publish-confidence <F>  Suppress low-score decoded output (default 0.72)\n");
    fprintf(stream, "  --publish-gap <N>         Min frame gap for same code re-publish (default 6)\n");
}

static const char *json_bool(int value) {
    return value ? "true" : "false";
}

static int appendf(char *buffer, size_t capacity, size_t *offset, const char *fmt, ...) {
    va_list ap;
    int written = 0;

    if (buffer == NULL || offset == NULL || fmt == NULL || *offset >= capacity) {
        return -1;
    }

    va_start(ap, fmt);
    written = vsnprintf(buffer + *offset, capacity - *offset, fmt, ap);
    va_end(ap);
    if (written < 0 || (size_t)written >= (capacity - *offset)) {
        return -1;
    }

    *offset += (size_t)written;
    return 0;
}

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

static int build_device_status_payload(char *payload, size_t capacity, const app_ctx_t *ctx, const char *reason) {
    size_t offset = 0u;
    const struct rf433_stats *drv_stats = NULL;
    const struct rf433_status *drv_status = NULL;

    if (payload == NULL || ctx == NULL || reason == NULL) {
        return -1;
    }

    drv_stats = &ctx->drv_stats;
    drv_status = &ctx->drv_status;

    if (
        appendf(
            payload,
            capacity,
            &offset,
            "{"
            "\"device_id\":\"%s\","
            "\"type\":\"device_status\","
            "\"rf_input\":\"%s\","
            "\"broker\":\"%s\","
            "\"port\":%d,"
            "\"rf_online\":%s,"
            "\"mqtt_connected\":%s,"
            "\"stable_repeat\":%u,"
            "\"stable_window\":%u,"
            "\"stable_near_bits\":%u,"
            "\"min_publish_confidence\":%.2f,"
            "\"publish_gap\":%u,"
            "\"driver_seq\":%u,"
            "\"driver_queue_depth\":%u,"
            "\"driver_queue_capacity\":%u,"
            "\"driver_frame_ok\":%llu,"
            "\"driver_crc_err\":%llu,"
            "\"driver_len_err\":%llu,"
            "\"driver_drop_cnt\":%llu,"
            "\"app_drv_drop\":%u,"
            "\"published_events\":%u,"
            "\"reason\":\"%s\""
            "}",
            MQTT_DEVICE_ID,
            ctx->rf_input,
            MQTT_BROKER_HOST,
            MQTT_BROKER_PORT,
            json_bool(ctx->has_drv_stats ? (drv_status->online != 0u) : 0),
            json_bool(mqtt_publisher_is_connected(&ctx->mqtt)),
            (unsigned)ctx->stable_repeat,
            (unsigned)ctx->stable_window,
            (unsigned)ctx->stable_near_bits,
            ctx->min_publish_confidence,
            (unsigned)ctx->publish_gap,
            ctx->has_drv_stats ? (unsigned)drv_status->seq : 0u,
            ctx->has_drv_stats ? (unsigned)drv_status->queue_depth : 0u,
            ctx->has_drv_stats ? (unsigned)drv_status->queue_capacity : 0u,
            ctx->has_drv_stats ? (unsigned long long)drv_stats->frame_ok : 0ull,
            ctx->has_drv_stats ? (unsigned long long)drv_stats->crc_err : 0ull,
            ctx->has_drv_stats ? (unsigned long long)drv_stats->len_err : 0ull,
            ctx->has_drv_stats ? (unsigned long long)drv_stats->drop_cnt : 0ull,
            (unsigned)ctx->drv_drop,
            (unsigned)ctx->published,
            reason
        ) != 0
    ) {
        return -1;
    }

    return 0;
}

static int build_rf_stats_payload(
    char *payload,
    size_t capacity,
    const app_ctx_t *ctx,
    const rf_decode_runtime_stats_t *decode_stats,
    const char *reason
) {
    size_t offset = 0u;
    const struct rf433_stats *drv_stats = NULL;
    const struct rf433_status *drv_status = NULL;
    const rf_epoll_stats_t *epoll_stats = NULL;

    if (payload == NULL || ctx == NULL || decode_stats == NULL || reason == NULL) {
        return -1;
    }

    drv_stats = &ctx->drv_stats;
    drv_status = &ctx->drv_status;
    epoll_stats = ctx->epoll_stats;

    if (
        appendf(
            payload,
            capacity,
            &offset,
            "{"
            "\"device_id\":\"%s\","
            "\"type\":\"rf_stats\","
            "\"rf_input\":\"%s\","
            "\"mqtt_connected\":%s,"
            "\"frames_total\":%u,"
            "\"decode_ok\":%u,"
            "\"decode_no_frame\":%u,"
            "\"decode_err\":%u,"
            "\"low_conf_drop\":%u,"
            "\"stable_drop\":%u,"
            "\"dup_drop\":%u,"
            "\"published_events\":%u,"
            "\"drv_drop\":%u,"
            "\"driver_frame_ok\":%llu,"
            "\"driver_crc_err\":%llu,"
            "\"driver_len_err\":%llu,"
            "\"driver_drop_cnt\":%llu,"
            "\"driver_online\":%s,"
            "\"driver_seq\":%u,"
            "\"driver_queue_depth\":%u,"
            "\"driver_queue_capacity\":%u,"
            "\"read_eintr\":%u,"
            "\"read_eagain\":%u,"
            "\"read_eof\":%u,"
            "\"read_error\":%u,"
            "\"short_read\":%u,"
            "\"epoll_eintr\":%u,"
            "\"epoll_error\":%u,"
            "\"decode_c_attempts\":%u,"
            "\"decode_c_accepts\":%u,"
            "\"decode_c_total_us\":%llu,"
            "\"decode_c_accept_total_us\":%llu,"
            "\"reason\":\"%s\""
            "}",
            MQTT_DEVICE_ID,
            ctx->rf_input,
            json_bool(mqtt_publisher_is_connected(&ctx->mqtt)),
            (unsigned)ctx->frames_total,
            (unsigned)ctx->decode_ok,
            (unsigned)ctx->decode_no_frame,
            (unsigned)ctx->decode_err,
            (unsigned)ctx->low_conf_drop,
            (unsigned)ctx->stable_drop,
            (unsigned)ctx->dup_drop,
            (unsigned)ctx->published,
            (unsigned)ctx->drv_drop,
            ctx->has_drv_stats ? (unsigned long long)drv_stats->frame_ok : 0ull,
            ctx->has_drv_stats ? (unsigned long long)drv_stats->crc_err : 0ull,
            ctx->has_drv_stats ? (unsigned long long)drv_stats->len_err : 0ull,
            ctx->has_drv_stats ? (unsigned long long)drv_stats->drop_cnt : 0ull,
            json_bool(ctx->has_drv_stats ? (drv_status->online != 0u) : 0),
            ctx->has_drv_stats ? (unsigned)drv_status->seq : 0u,
            ctx->has_drv_stats ? (unsigned)drv_status->queue_depth : 0u,
            ctx->has_drv_stats ? (unsigned)drv_status->queue_capacity : 0u,
            epoll_stats != NULL ? (unsigned)epoll_stats->read_eintr : 0u,
            epoll_stats != NULL ? (unsigned)epoll_stats->read_eagain : 0u,
            epoll_stats != NULL ? (unsigned)epoll_stats->read_eof : 0u,
            epoll_stats != NULL ? (unsigned)epoll_stats->read_error : 0u,
            epoll_stats != NULL ? (unsigned)epoll_stats->short_read : 0u,
            epoll_stats != NULL ? (unsigned)epoll_stats->epoll_eintr : 0u,
            epoll_stats != NULL ? (unsigned)epoll_stats->epoll_error : 0u,
            decode_stats->c_attempts,
            decode_stats->c_accepts,
            decode_stats->c_total_us,
            decode_stats->c_accept_total_us,
            reason
        ) != 0
    ) {
        return -1;
    }

    return 0;
}

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

static void emit_device_status(app_ctx_t *ctx, const char *reason) {
    char payload[JSON_PAYLOAD_CAPACITY];

    if (build_device_status_payload(payload, sizeof(payload), ctx, reason) != 0) {
        fprintf(stderr, "[RF_JSON] failed to assemble device status payload\n");
        return;
    }

    (void)emit_protocol_message(ctx, "device_status", MQTT_TOPIC_STATUS, payload, 1);
}

static void emit_rf_stats(app_ctx_t *ctx, const char *reason) {
    char payload[JSON_PAYLOAD_CAPACITY];
    rf_decode_runtime_stats_t decode_stats;

    if (ctx == NULL) {
        return;
    }

    memset(&decode_stats, 0, sizeof(decode_stats));
    rf_decode_get_runtime_stats(&decode_stats);
    if (build_rf_stats_payload(payload, sizeof(payload), ctx, &decode_stats, reason) != 0) {
        fprintf(stderr, "[RF_JSON] failed to assemble rf stats payload\n");
        return;
    }

    (void)emit_protocol_message(ctx, "rf_stats", MQTT_TOPIC_RF_STATS, payload, 0);
}

static void refresh_driver_state(app_ctx_t *ctx, int rf_fd) {
    struct rf433_stats drv_stats;
    struct rf433_status drv_status;

    if (ctx == NULL || rf_fd < 0) {
        return;
    }

    if (
        ioctl(rf_fd, RF433_IOC_GET_STATS, &drv_stats) == 0 &&
        ioctl(rf_fd, RF433_IOC_GET_STATUS, &drv_status) == 0
    ) {
        ctx->drv_stats = drv_stats;
        ctx->drv_status = drv_status;
        ctx->has_drv_stats = 1;
    } else {
        fprintf(stderr, "[RF_IO] failed to query driver stats/status\n");
    }
}

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

static void on_drv_stats(int rf_fd, void *user) {
    app_ctx_t *ctx = (app_ctx_t *)user;

    if (ctx == NULL) {
        return;
    }

    refresh_driver_state(ctx, rf_fd);
    emit_device_status(ctx, "driver_stats");
    emit_rf_stats(ctx, "periodic");
}

int main(int argc, char **argv) {
    const char *rf_input = RF_SOURCE_PATH;
    uint16_t stable_repeat = 2u;
    uint16_t stable_window = 12u;
    uint8_t stable_near_bits = 4u;
    float min_publish_conf = 0.72f;
    uint16_t publish_gap = 6u;
    app_ctx_t ctx;
    int rf_fd = -1;
    int i = 0;
    int mqtt_rc = 0;
    rf_epoll_config_t cfg;
    rf_epoll_stats_t epoll_stats;
    int epoll_rc = 0;

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
        } else if (strcmp(argv[i], "--min-publish-confidence") == 0 && i + 1 < argc) {
            min_publish_conf = (float)atof(argv[++i]);
        } else if (strcmp(argv[i], "--publish-gap") == 0 && i + 1 < argc) {
            publish_gap = (uint16_t)strtoul(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--help") == 0) {
            print_usage(stderr, argv[0]);
            return 0;
        } else {
            fprintf(stderr, "Unknown arg: %s\n", argv[i]);
            print_usage(stderr, argv[0]);
            return 1;
        }
    }

    if (!rf_source_is_supported_path(rf_input)) {
        fprintf(stderr, "Unsupported --rf-input: %s (allowed: %s)\n", rf_input, RF_SOURCE_PATH);
        return 1;
    }

    rf_fd = rf_source_open(rf_input);
    if (rf_fd < 0) {
        fprintf(stderr, "Open RF input failed: %s\n", rf_input);
        return 2;
    }

    ctx.rf_input = rf_input;
    ctx.publish_gap = publish_gap;
    ctx.stable_repeat = (stable_repeat > 0u) ? stable_repeat : 1u;
    ctx.stable_window = (stable_window > 0u) ? stable_window : 12u;
    if (stable_near_bits > 12u) {
        stable_near_bits = 12u;
    }
    ctx.stable_near_bits = stable_near_bits;
    ctx.min_publish_confidence = min_publish_conf;
    ctx.epoll_stats = &epoll_stats;

    mqtt_rc = mqtt_publisher_init(
        &ctx.mqtt,
        "rk3568-001-rf-gateway",
        MQTT_BROKER_HOST,
        MQTT_BROKER_PORT,
        MQTT_TOPIC_ROOT
    );
    if (mqtt_rc != 0) {
        fprintf(stderr, "[MQTT] publisher init failed: %s\n", mqtt_publisher_error_string(mqtt_rc));
    }

    refresh_driver_state(&ctx, rf_fd);
    emit_device_status(&ctx, "startup");

    cfg.rf_fd = rf_fd;
    cfg.on_frame = on_rf_frame;
    cfg.on_stats = on_drv_stats;
    cfg.stats_interval_s = 5;
    cfg.stats = &epoll_stats;
    cfg.user = &ctx;

    epoll_rc = rf_epoll_run(&cfg);
    if (epoll_rc != 0) {
        fprintf(stderr, "[RF_IO] rf_epoll_run exited with rc=%d\n", epoll_rc);
    }

    refresh_driver_state(&ctx, rf_fd);
    emit_rf_stats(&ctx, "shutdown");
    ctx.drv_status.online = 0u;
    ctx.has_drv_stats = 1;
    emit_device_status(&ctx, "shutdown");

    mqtt_publisher_cleanup(&ctx.mqtt);
    rf_source_close(rf_fd);
    return (epoll_rc == 0) ? 0 : 3;
}
