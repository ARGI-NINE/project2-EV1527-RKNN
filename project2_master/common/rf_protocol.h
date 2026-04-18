#ifndef RF_PROTOCOL_H
#define RF_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RF_PROTO_SYNC0 0xAAu
#define RF_PROTO_SYNC1 0x55u
#define RF_BUFFER_SIZE 1024u

typedef struct {
    uint16_t pulse[RF_BUFFER_SIZE];
    uint16_t len;
} rf_frame_t;

typedef enum {
    RF_PARSE_SYNC0 = 0,
    RF_PARSE_SYNC1,
    RF_PARSE_LEN0,
    RF_PARSE_LEN1,
    RF_PARSE_PAYLOAD,
    RF_PARSE_CRC
} rf_parse_state_t;

typedef enum {
    RF_PROTO_PARSER_ERR_NONE = 0,
    RF_PROTO_PARSER_ERR_INVALID_LEN,
    RF_PROTO_PARSER_ERR_CRC_MISMATCH,
    RF_PROTO_PARSER_ERR_INTERNAL
} rf_proto_parser_error_t;

typedef struct {
    rf_parse_state_t state;
    uint16_t expected_pulses;
    uint16_t payload_index;
    uint8_t payload[RF_BUFFER_SIZE * 2u];
    uint8_t crc;
    rf_proto_parser_error_t last_error;
} rf_proto_parser_t;

uint8_t rf_proto_crc8(const uint8_t *data, size_t len);
size_t rf_proto_encode(const rf_frame_t *frame, uint8_t *out, size_t out_capacity);
void rf_proto_parser_init(rf_proto_parser_t *parser);
int rf_proto_parser_consume(rf_proto_parser_t *parser, uint8_t byte, rf_frame_t *out_frame);

#ifdef __cplusplus
}
#endif

#endif
