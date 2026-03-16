#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rf_db.h"
#include "rf_decode.h"
#include "rf_decode_window.h"
#include "rf_epoll.h"
#include "rf_mqtt.h"
#include "rf_source.h"

typedef struct {
    const char *python_bin;
    const char *python_script;
    rf_db_t db;
    rf_mqtt_client_t mqtt;
    rf_decode_window_t win;
    uint32_t frame_seq;
    uint32_t last_publish_seq;
    unsigned last_code;
    uint16_t publish_gap;
    int has_last_code;
    int decode_mode_window;
} app_ctx_t;

static void print_usage(const char *exe) {
    printf("Usage: %s [options]\n", exe);
    printf("  --rf-input <path>         Input stream (default /dev/rf433)\n");
    printf("  --db <path>               SQLite path (default ./rf_device.db)\n");
    printf("  --python-bin <path>       Python interpreter\n");
    printf("  --python-decoder <path>   Python pulse decoder script\n");
    printf("  --inject-cmd <json>       Inject one MQTT command for test\n");
    printf("  --decode-mode <window|frame>  Decode mode (default window)\n");
    printf("  --window-size <N>         Window frame count (default 12)\n");
    printf("  --stable-repeat <N>       Need N agreeing frames (default 3)\n");
    printf("  --min-win-confidence <F>  Min confidence per frame vote (default 0.60)\n");
    printf("  --publish-gap <N>         Min frame gap for same code re-publish (default 6)\n");
}

static void rf_replay(const char *device, const char *action) {
    printf("[REPLAY] device=%s action=%s (stub, call STM32 TX replay here)\n", device, action);
}

static int on_rf_frame(const rf_frame_t *frame, void *user) {
    app_ctx_t *ctx = (app_ctx_t *)user;
    rf_decoded_packet_t pkt;
    uint16_t agree_count = 0u;
    int rc = 0;

    if (ctx == NULL || frame == NULL) {
        return -1;
    }

    ctx->frame_seq++;
    if (ctx->decode_mode_window) {
        rf_decode_window_push(&ctx->win, frame);
        rc = rf_decode_window_try_decode(
            &ctx->win,
            ctx->python_bin,
            ctx->python_script,
            &pkt,
            &agree_count
        );
        if (rc != 0) {
            return 0;
        }
    } else {
        rc = rf_decode_frame(frame, ctx->python_bin, ctx->python_script, &pkt);
        if (rc != 0) {
            printf("[RF] decode failed, len=%u rc=%d\n", frame->len, rc);
            return 0;
        }
        agree_count = 1u;
    }

    if (
        ctx->has_last_code &&
        pkt.raw_code == ctx->last_code &&
        (ctx->frame_seq - ctx->last_publish_seq) < (uint32_t)ctx->publish_gap
    ) {
        return 0;
    }

    printf(
        "[RF] addr=%s key=%s conf=%.2f source=%s pulses=%u agree=%u\n",
        pkt.addr,
        pkt.key,
        pkt.confidence,
        pkt.source,
        frame->len,
        (unsigned)agree_count
    );
    ctx->last_code = pkt.raw_code;
    ctx->has_last_code = 1;
    ctx->last_publish_seq = ctx->frame_seq;
    (void)rf_db_upsert(&ctx->db, &pkt, "ev1527_device");
    (void)rf_mqtt_publish_decode(&pkt);
    return 0;
}

static int on_mqtt_event(int mqtt_fd, void *user) {
    app_ctx_t *ctx = (app_ctx_t *)user;
    rf_mqtt_cmd_t cmd;
    int rc = 0;

    (void)mqtt_fd;
    if (ctx == NULL) {
        return -1;
    }

    rc = rf_mqtt_read_command(&ctx->mqtt, &cmd);
    if (rc != 0) {
        return 0;
    }
    printf("[MQTT CMD] device=%s action=%s\n", cmd.device, cmd.action);
    rf_replay(cmd.device, cmd.action);
    return 0;
}

int main(int argc, char **argv) {
    const char *rf_input = "/dev/rf433";
    const char *db_path = "./rf_device.db";
    const char *python_bin = getenv("RF_PYTHON_BIN");
    const char *python_decoder = getenv("RF_PY_DECODER");
    const char *inject_cmd = NULL;
    uint16_t window_size = 12u;
    uint16_t stable_repeat = 3u;
    float min_win_conf = 0.60f;
    uint16_t publish_gap = 6u;
    app_ctx_t ctx;
    int rf_fd = -1;
    int i = 0;
    rf_epoll_config_t cfg;

    memset(&ctx, 0, sizeof(ctx));
    ctx.decode_mode_window = 1;
    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--rf-input") == 0 && i + 1 < argc) {
            rf_input = argv[++i];
        } else if (strcmp(argv[i], "--db") == 0 && i + 1 < argc) {
            db_path = argv[++i];
        } else if (strcmp(argv[i], "--python-bin") == 0 && i + 1 < argc) {
            python_bin = argv[++i];
        } else if (strcmp(argv[i], "--python-decoder") == 0 && i + 1 < argc) {
            python_decoder = argv[++i];
        } else if (strcmp(argv[i], "--inject-cmd") == 0 && i + 1 < argc) {
            inject_cmd = argv[++i];
        } else if (strcmp(argv[i], "--decode-mode") == 0 && i + 1 < argc) {
            const char *mode = argv[++i];
            if (strcmp(mode, "window") == 0) {
                ctx.decode_mode_window = 1;
            } else if (strcmp(mode, "frame") == 0) {
                ctx.decode_mode_window = 0;
            } else {
                printf("Invalid decode mode: %s\n", mode);
                return 1;
            }
        } else if (strcmp(argv[i], "--window-size") == 0 && i + 1 < argc) {
            window_size = (uint16_t)strtoul(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--stable-repeat") == 0 && i + 1 < argc) {
            stable_repeat = (uint16_t)strtoul(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--min-win-confidence") == 0 && i + 1 < argc) {
            min_win_conf = (float)atof(argv[++i]);
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

    if (python_bin == NULL || python_bin[0] == '\0') {
        python_bin = "python3";
    }
    if (python_decoder == NULL || python_decoder[0] == '\0') {
        python_decoder = "./python/ev1527_decode_bridge.py";
    }

    rf_fd = rf_source_open(rf_input);
    if (rf_fd < 0) {
        printf("Open RF input failed: %s\n", rf_input);
        return 2;
    }

    if (rf_db_init(&ctx.db, db_path) != 0) {
        printf("DB init failed: %s\n", db_path);
    }
    if (rf_mqtt_init(&ctx.mqtt) != 0) {
        printf("MQTT socket init failed\n");
    }
    if (inject_cmd != NULL) {
        (void)rf_mqtt_inject_command(&ctx.mqtt, inject_cmd);
    }

    ctx.python_bin = python_bin;
    ctx.python_script = python_decoder;
    if (ctx.decode_mode_window != 0 && ctx.decode_mode_window != 1) {
        ctx.decode_mode_window = 1;
    }
    ctx.publish_gap = publish_gap;
    rf_decode_window_init(&ctx.win, window_size, stable_repeat, min_win_conf);

    cfg.rf_fd = rf_fd;
#if defined(__linux__)
    cfg.mqtt_fd = rf_mqtt_fd(&ctx.mqtt);
#else
    cfg.mqtt_fd = -1;
#endif
    cfg.on_frame = on_rf_frame;
    cfg.on_mqtt = on_mqtt_event;
    cfg.user = &ctx;

    printf(
        "rf_gateway running: rf=%s db=%s decoder=%s mode=%s window=%u stable=%u conf=%.2f\n",
        rf_input,
        db_path,
        python_decoder,
        ctx.decode_mode_window ? "window" : "frame",
        (unsigned)window_size,
        (unsigned)stable_repeat,
        min_win_conf
    );
    (void)rf_epoll_run(&cfg);

    rf_mqtt_close(&ctx.mqtt);
    rf_db_close(&ctx.db);
    rf_source_close(rf_fd);
    return 0;
}
