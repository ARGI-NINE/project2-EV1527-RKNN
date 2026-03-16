#ifndef RF_DECODE_WINDOW_H
#define RF_DECODE_WINDOW_H

#include <stdint.h>

#include "../common/rf_protocol.h"
#include "rf_decode.h"

#define RF_DECODE_WINDOW_MAX 32u

typedef struct {
    rf_frame_t frames[RF_DECODE_WINDOW_MAX];
    uint16_t capacity;
    uint16_t count;
    uint16_t head;
    uint16_t stable_repeat;
    float min_confidence;
} rf_decode_window_t;

void rf_decode_window_init(
    rf_decode_window_t *win,
    uint16_t capacity,
    uint16_t stable_repeat,
    float min_confidence
);
void rf_decode_window_push(rf_decode_window_t *win, const rf_frame_t *frame);
int rf_decode_window_try_decode(
    rf_decode_window_t *win,
    const char *python_bin,
    const char *python_script,
    rf_decoded_packet_t *out,
    uint16_t *agree_count
);

#endif

