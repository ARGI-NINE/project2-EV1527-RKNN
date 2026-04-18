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

void rf_proto_parser_init(rf_proto_parser_t *parser) {
    if (parser == NULL) {
        return;
    }
    memset(parser, 0, sizeof(*parser));
    parser->state = RF_PARSE_SYNC0;
}

int rf_proto_parser_consume(rf_proto_parser_t *parser, uint8_t byte, rf_frame_t *out_frame) {
    rf_proto_parser_error_t parse_error = RF_PROTO_PARSER_ERR_NONE;

    if (parser == NULL || out_frame == NULL) {
        return -1;
    }
    parser->last_error = RF_PROTO_PARSER_ERR_NONE;

    switch (parser->state) {
        case RF_PARSE_SYNC0:
            if (byte == RF_PROTO_SYNC0) {
                parser->state = RF_PARSE_SYNC1;
            }
            break;
        case RF_PARSE_SYNC1:
            if (byte == RF_PROTO_SYNC1) {
                parser->state = RF_PARSE_LEN0;
                parser->crc = 0u;
                parser->expected_pulses = 0u;
                parser->payload_index = 0u;
            } else if (byte != RF_PROTO_SYNC0) {
                parser->state = RF_PARSE_SYNC0;
            }
            break;
        case RF_PARSE_LEN0:
            parser->expected_pulses = byte;
            parser->crc = byte;
            parser->state = RF_PARSE_LEN1;
            break;
        case RF_PARSE_LEN1:
            parser->expected_pulses |= (uint16_t)((uint16_t)byte << 8u);
            parser->crc ^= byte;
            if (parser->expected_pulses == 0u || parser->expected_pulses > RF_BUFFER_SIZE) {
                parse_error = RF_PROTO_PARSER_ERR_INVALID_LEN;
                rf_proto_parser_init(parser);
                parser->last_error = parse_error;
                return -1;
            }
            parser->state = RF_PARSE_PAYLOAD;
            break;
        case RF_PARSE_PAYLOAD:
            parser->payload[parser->payload_index++] = byte;
            parser->crc ^= byte;
            if (parser->payload_index >= (uint16_t)(parser->expected_pulses * 2u)) {
                parser->state = RF_PARSE_CRC;
            }
            break;
        case RF_PARSE_CRC: {
            uint16_t i = 0u;
            if (byte != parser->crc) {
                parse_error = RF_PROTO_PARSER_ERR_CRC_MISMATCH;
                rf_proto_parser_init(parser);
                parser->last_error = parse_error;
                return -1;
            }
            out_frame->len = parser->expected_pulses;
            for (i = 0u; i < parser->expected_pulses; ++i) {
                const uint16_t lo = parser->payload[(size_t)i * 2u];
                const uint16_t hi = parser->payload[(size_t)i * 2u + 1u];
                out_frame->pulse[i] = (uint16_t)(lo | (uint16_t)(hi << 8u));
            }
            rf_proto_parser_init(parser);
            return 1;
        }
        default:
            parse_error = RF_PROTO_PARSER_ERR_INTERNAL;
            rf_proto_parser_init(parser);
            parser->last_error = parse_error;
            return -1;
    }

    return 0;
}
