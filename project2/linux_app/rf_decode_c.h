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
} rf_decode_result_c_t;

int rf_decode_ev1527_c(const rf_frame_t *frame, rf_decode_result_c_t *out);

#endif

