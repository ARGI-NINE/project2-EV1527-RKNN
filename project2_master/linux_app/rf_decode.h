#ifndef RF_DECODE_H
#define RF_DECODE_H

#include <stddef.h>

#include "../common/rf_protocol.h"
#include "rf_decode_c.h"

#define RF_DECODE_RC_OK 0
#define RF_DECODE_RC_NO_FRAME (-8)

typedef struct {
    char addr[16];
    char key[8];
    float confidence;
    unsigned raw_code;
    char source[16];
} rf_decoded_packet_t;

typedef struct {
    unsigned c_attempts;
    unsigned c_accepts;
    unsigned long long c_total_us;
    unsigned long long c_accept_total_us;
} rf_decode_runtime_stats_t;

typedef struct {
    int rc;
    uint16_t frame_len;
    int c_ok;
    float c_confidence;
    unsigned long long total_us;
    unsigned long long c_total_us;
} rf_decode_last_call_stats_t;

int rf_decode_frame(const rf_frame_t *frame, rf_decoded_packet_t *out);
void rf_decode_get_runtime_stats(rf_decode_runtime_stats_t *out);
void rf_decode_get_last_call_stats(rf_decode_last_call_stats_t *out);

#endif
