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

uint8_t rf_proto_crc8(const uint8_t *data, size_t len);
size_t rf_proto_encode(const rf_frame_t *frame, uint8_t *out, size_t out_capacity);

#ifdef __cplusplus
}
#endif

#endif
