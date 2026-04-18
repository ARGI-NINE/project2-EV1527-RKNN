#ifndef RF_DECODE_C_H
#define RF_DECODE_C_H

#include <stdint.h>

#include "../common/rf_protocol.h"

typedef struct {
    uint32_t raw_code;
    uint32_t address20;
    uint8_t button4;
    float clk_us;
    float confidence;
    float bit_error;
    float sync_error;
    float period_jitter;
    uint16_t start_index;
} rf_decode_result_c_t;

typedef struct {
    uint16_t step1_structural;
    uint16_t step2_timing;
} rf_decode_stage_stats_t;

int rf_decode_ev1527_c(const rf_frame_t *frame, rf_decode_result_c_t *out);
int rf_decode_ev1527_c_with_stats(const rf_frame_t *frame, rf_decode_result_c_t *out, rf_decode_stage_stats_t *stats);

#endif

