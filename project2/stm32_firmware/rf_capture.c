#include "rf_capture.h"

#include <string.h>

static uint16_t rf_ticks_to_us(const rf_capture_ctx_t *ctx, uint32_t ticks) {
    uint64_t us = 0u;
    uint32_t hz = 1000000u;
    if (ctx != NULL && ctx->timer_hz > 0u) {
        hz = ctx->timer_hz;
    }
    us = ((uint64_t)ticks * 1000000ull) / (uint64_t)hz;
    if (us > 65535ull) {
        us = 65535ull;
    }
    return (uint16_t)us;
}

void rf_capture_init(rf_capture_ctx_t *ctx, rf_frame_ready_cb_t cb, void *user) {
    if (ctx == NULL) {
        return;
    }
    memset(ctx, 0, sizeof(*ctx));
    ctx->on_frame = cb;
    ctx->user = user;
    ctx->timer_hz = 1000000u;
    ctx->timer_period = 0xFFFFu;
    ctx->min_pulse_us = 80u;
    ctx->max_pulse_us = 60000u;
    ctx->sync_us = 8000u;
    ctx->min_frame_pulses = 48u;
}

void rf_capture_online_init(
    rf_capture_ctx_t *ctx,
    rf_frame_ready_cb_t cb,
    void *user,
    uint32_t timer_hz,
    uint16_t timer_period
) {
    rf_capture_init(ctx, cb, user);
    if (ctx == NULL) {
        return;
    }
    if (timer_hz > 0u) {
        ctx->timer_hz = timer_hz;
    }
    if (timer_period > 0u) {
        ctx->timer_period = timer_period;
    }
}

void rf_capture_set_filter(
    rf_capture_ctx_t *ctx,
    uint16_t min_pulse_us,
    uint16_t max_pulse_us,
    uint16_t sync_us,
    uint16_t min_frame_pulses
) {
    if (ctx == NULL) {
        return;
    }
    if (min_pulse_us > 0u) {
        ctx->min_pulse_us = min_pulse_us;
    }
    if (max_pulse_us > ctx->min_pulse_us) {
        ctx->max_pulse_us = max_pulse_us;
    }
    if (sync_us > ctx->min_pulse_us) {
        ctx->sync_us = sync_us;
    }
    if (min_frame_pulses > 0u) {
        ctx->min_frame_pulses = min_frame_pulses;
    }
}

void rf_capture_flush(rf_capture_ctx_t *ctx) {
    if (ctx == NULL) {
        return;
    }
    if (ctx->current.len >= ctx->min_frame_pulses && ctx->on_frame != NULL) {
        ctx->on_frame(&ctx->current, ctx->user);
    }
    ctx->current.len = 0u;
}

void rf_frame_detect(rf_capture_ctx_t *ctx, uint16_t pulse_us) {
    if (ctx == NULL || pulse_us == 0u) {
        return;
    }

    if (pulse_us < ctx->min_pulse_us || pulse_us > ctx->max_pulse_us) {
        return;
    }

    if (pulse_us > ctx->sync_us && ctx->current.len >= ctx->min_frame_pulses) {
        rf_capture_flush(ctx);
    }

    if (ctx->current.len < RF_BUFFER_SIZE) {
        ctx->current.pulse[ctx->current.len++] = pulse_us;
    } else {
        rf_capture_flush(ctx);
    }
}

void rf_capture_isr(rf_capture_ctx_t *ctx, uint16_t pulse_us) {
    /* Call from TIM2 InputCapture ISR with edge delta in us. */
    rf_frame_detect(ctx, pulse_us);
}

void rf_capture_process_ccr(rf_capture_ctx_t *ctx, uint16_t ccr) {
    uint32_t ticks = 0u;
    uint16_t pulse_us = 0u;

    if (ctx == NULL) {
        return;
    }
    if (!ctx->has_last_ccr) {
        ctx->last_ccr = ccr;
        ctx->has_last_ccr = 1u;
        return;
    }

    if (ccr >= ctx->last_ccr) {
        ticks = (uint32_t)(ccr - ctx->last_ccr);
    } else {
        ticks = (uint32_t)ctx->timer_period + 1u + (uint32_t)ccr - (uint32_t)ctx->last_ccr;
    }
    ctx->last_ccr = ccr;
    pulse_us = rf_ticks_to_us(ctx, ticks);
    rf_frame_detect(ctx, pulse_us);
}

void rf_capture_process_dma(rf_capture_ctx_t *ctx, const uint16_t *ccr_buf, uint16_t count) {
    uint16_t i = 0u;
    if (ctx == NULL || ccr_buf == NULL) {
        return;
    }
    for (i = 0u; i < count; ++i) {
        rf_capture_process_ccr(ctx, ccr_buf[i]);
    }
}

void rf_capture_generate_virtual_frame(rf_frame_t *frame, uint32_t code24, uint16_t t_us) {
    uint8_t bit = 0u;
    if (frame == NULL) {
        return;
    }
    if (t_us == 0u) {
        t_us = 300u;
    }

    frame->len = 0u;
    frame->pulse[frame->len++] = (uint16_t)(4u * t_us);
    frame->pulse[frame->len++] = (uint16_t)(124u * t_us);

    for (bit = 0u; bit < 24u; ++bit) {
        const uint8_t b = (uint8_t)((code24 >> (23u - bit)) & 0x1u);
        if (b != 0u) {
            frame->pulse[frame->len++] = (uint16_t)(12u * t_us);
            frame->pulse[frame->len++] = (uint16_t)(4u * t_us);
        } else {
            frame->pulse[frame->len++] = (uint16_t)(4u * t_us);
            frame->pulse[frame->len++] = (uint16_t)(12u * t_us);
        }
    }
}

void rf_capture_feed_virtual(rf_capture_ctx_t *ctx, uint32_t code24, uint16_t t_us, uint8_t repeat) {
    uint8_t i = 0u;
    rf_frame_t frame;
    if (ctx == NULL || ctx->on_frame == NULL || repeat == 0u) {
        return;
    }

    rf_capture_generate_virtual_frame(&frame, code24, t_us);
    for (i = 0u; i < repeat; ++i) {
        ctx->on_frame(&frame, ctx->user);
    }
}
