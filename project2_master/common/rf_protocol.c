#include "rf_protocol.h"

#include <string.h>

uint8_t rf_proto_crc8(const uint8_t *data, size_t len) {
    size_t i = 0u;
    uint8_t crc = 0u;
    for (i = 0u; i < len; ++i) {
        crc ^= data[i];
    }
    return crc;
}

size_t rf_proto_encode(const rf_frame_t *frame, uint8_t *out, size_t out_capacity) {
    uint16_t i = 0u;
    uint16_t bytes = 0u;
    uint8_t crc = 0u;
    size_t total = 0u;

    if (frame == NULL || out == NULL) {
        return 0u;
    }
    if (frame->len == 0u || frame->len > RF_BUFFER_SIZE) {
        return 0u;
    }

    bytes = (uint16_t)(frame->len * 2u);
    total = (size_t)2u + 2u + bytes + 1u;
    if (out_capacity < total) {
        return 0u;
    }

    out[0] = RF_PROTO_SYNC0;
    out[1] = RF_PROTO_SYNC1;
    out[2] = (uint8_t)(frame->len & 0xFFu);
    out[3] = (uint8_t)((frame->len >> 8u) & 0xFFu);

    for (i = 0u; i < frame->len; ++i) {
        const uint16_t p = frame->pulse[i];
        const size_t off = (size_t)4u + (size_t)i * 2u;
        out[off] = (uint8_t)(p & 0xFFu);
        out[off + 1u] = (uint8_t)((p >> 8u) & 0xFFu);
    }

    crc = rf_proto_crc8(&out[2], (size_t)2u + bytes);
    out[4u + bytes] = crc;
    return total;
}
