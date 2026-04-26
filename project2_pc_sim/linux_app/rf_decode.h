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

int rf_decode_frame(const rf_frame_t *frame, rf_decoded_packet_t *out);

#endif
