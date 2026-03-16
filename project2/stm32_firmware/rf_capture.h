#ifndef RF_CAPTURE_H
#define RF_CAPTURE_H

#include <stdint.h>

#include "../common/rf_protocol.h"

typedef void (*rf_frame_ready_cb_t)(const rf_frame_t *frame, void *user);

typedef struct {
    rf_frame_t current;
    rf_frame_ready_cb_t on_frame;
    void *user;
    uint32_t timer_hz;
    uint16_t timer_period;
    uint16_t last_ccr;
    uint8_t has_last_ccr;
    uint16_t min_pulse_us;
    uint16_t max_pulse_us;
    uint16_t sync_us;
    uint16_t min_frame_pulses;
} rf_capture_ctx_t;

/* Legacy init path: pulse_us is already computed by upper layer/ISR. */
void rf_capture_init(rf_capture_ctx_t *ctx, rf_frame_ready_cb_t cb, void *user);
void rf_capture_isr(rf_capture_ctx_t *ctx, uint16_t pulse_us);
void rf_frame_detect(rf_capture_ctx_t *ctx, uint16_t pulse_us);

/* Online real-time path for TIM2 InputCapture continuous sampling. */
void rf_capture_online_init(
    rf_capture_ctx_t *ctx,
    rf_frame_ready_cb_t cb,
    void *user,
    uint32_t timer_hz,
    uint16_t timer_period
);
void rf_capture_set_filter(
    rf_capture_ctx_t *ctx,
    uint16_t min_pulse_us,
    uint16_t max_pulse_us,
    uint16_t sync_us,
    uint16_t min_frame_pulses
);
void rf_capture_process_ccr(rf_capture_ctx_t *ctx, uint16_t ccr);
void rf_capture_process_dma(rf_capture_ctx_t *ctx, const uint16_t *ccr_buf, uint16_t count);
void rf_capture_flush(rf_capture_ctx_t *ctx);

/* Offline/virtual path retained for simulation and regression tests. */
void rf_capture_generate_virtual_frame(rf_frame_t *frame, uint32_t code24, uint16_t t_us);
void rf_capture_feed_virtual(rf_capture_ctx_t *ctx, uint32_t code24, uint16_t t_us, uint8_t repeat);

#endif
