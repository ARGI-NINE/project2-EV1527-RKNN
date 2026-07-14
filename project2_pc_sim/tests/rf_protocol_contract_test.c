#include <stdio.h>
#include <string.h>

#include "rf_protocol.h"

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            return 1; \
        } \
    } while (0)

static int consume_packet(rf_proto_parser_t *parser, const uint8_t *packet, size_t len, rf_frame_t *out) {
    size_t i = 0u;
    int rc = 0;
    for (i = 0u; i < len; ++i) {
        rc = rf_proto_parser_consume(parser, packet[i], out);
    }
    return rc;
}

int main(void) {
    rf_frame_t source;
    rf_frame_t decoded;
    rf_proto_parser_t parser;
    uint8_t packet[32];
    uint8_t damaged[32];
    const uint8_t oversized_length[] = {0xAAu, 0x55u, 0x01u, 0x04u};
    const uint8_t resync_prefix[] = {0x00u, 0xAAu, 0xAAu};
    size_t encoded = 0u;
    size_t i = 0u;

    memset(&source, 0, sizeof(source));
    source.len = 3u;
    source.pulse[0] = 350u;
    source.pulse[1] = 10850u;
    source.pulse[2] = 1050u;
    encoded = rf_proto_encode(&source, packet, sizeof(packet));
    CHECK(encoded == 11u);

    rf_proto_parser_init(&parser);
    memset(&decoded, 0, sizeof(decoded));
    for (i = 0u; i < sizeof(resync_prefix); ++i) {
        CHECK(rf_proto_parser_consume(&parser, resync_prefix[i], &decoded) == 0);
    }
    CHECK(consume_packet(&parser, packet + 1u, encoded - 1u, &decoded) == 1);
    CHECK(decoded.len == source.len);
    CHECK(memcmp(decoded.pulse, source.pulse, source.len * sizeof(source.pulse[0])) == 0);

    memcpy(damaged, packet, encoded);
    damaged[encoded - 1u] ^= 0x01u;
    CHECK(consume_packet(&parser, damaged, encoded, &decoded) == -1);
    CHECK(parser.state == RF_PARSE_SYNC0);
    CHECK(consume_packet(&parser, packet, encoded, &decoded) == 1);

    CHECK(consume_packet(&parser, oversized_length, sizeof(oversized_length), &decoded) == -1);
    CHECK(parser.state == RF_PARSE_SYNC0);
    CHECK(rf_proto_parser_consume(NULL, 0u, &decoded) == -1);
    CHECK(rf_proto_parser_consume(&parser, 0u, NULL) == -1);
    return 0;
}
