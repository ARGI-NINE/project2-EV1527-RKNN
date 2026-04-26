#include "rf_decode.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int rf_decode_frame(
    const rf_frame_t *frame,
    rf_decoded_packet_t *out
) {
    rf_decode_result_c_t c_result;
    int c_rc;

    if (frame == NULL || out == NULL) {
        return -1;
    }

    memset(out, 0, sizeof(*out));
    c_rc = rf_decode_ev1527_c(frame, &c_result);

    if (c_rc == 0) {
        snprintf(out->addr, sizeof(out->addr), "0x%06X", c_result.raw_code);
        snprintf(out->key, sizeof(out->key), "%u", (unsigned)c_result.button4);
        snprintf(out->source, sizeof(out->source), "c");
        out->raw_code = c_result.raw_code;
        out->confidence = c_result.confidence;
        return 0;
    }

    return RF_DECODE_RC_NO_FRAME;
}
