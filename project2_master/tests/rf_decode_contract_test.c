#include <stdio.h>
#include <string.h>

#include "rf_decode.h"
#include "rf_protocol.h"

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            return 1; \
        } \
    } while (0)

static void build_ev1527_frame(rf_frame_t *frame, unsigned code) {
    unsigned bit = 0u;

    memset(frame, 0, sizeof(*frame));
    frame->len = 50u;
    frame->pulse[0] = 350u;
    frame->pulse[1] = 10850u;
    for (bit = 0u; bit < 24u; ++bit) {
        const unsigned value = (code >> (23u - bit)) & 1u;
        frame->pulse[2u + bit * 2u] = value ? 1050u : 350u;
        frame->pulse[3u + bit * 2u] = value ? 350u : 1050u;
    }
}

static int test_protocol_encoder(void) {
    rf_frame_t frame;
    uint8_t packet[16];
    const uint8_t expected[] = {0xAAu, 0x55u, 0x02u, 0x00u, 0x34u, 0x12u, 0xCDu, 0xABu, 0x42u};
    size_t encoded = 0u;

    memset(&frame, 0, sizeof(frame));
    frame.len = 2u;
    frame.pulse[0] = 0x1234u;
    frame.pulse[1] = 0xABCDu;
    encoded = rf_proto_encode(&frame, packet, sizeof(packet));
    CHECK(encoded == sizeof(expected));
    CHECK(memcmp(packet, expected, sizeof(expected)) == 0);
    CHECK(rf_proto_encode(&frame, packet, sizeof(expected) - 1u) == 0u);
    frame.len = 0u;
    CHECK(rf_proto_encode(&frame, packet, sizeof(packet)) == 0u);
    return 0;
}

static int test_decoder_success_and_failure_stats(void) {
    const unsigned code = 0xABCDE3u;
    rf_frame_t frame;
    rf_decoded_packet_t decoded;
    rf_decode_last_call_stats_t last;
    rf_decode_runtime_stats_t runtime;

    build_ev1527_frame(&frame, code);
    CHECK(rf_decode_frame(&frame, &decoded) == RF_DECODE_RC_OK);
    CHECK(decoded.raw_code == code);
    CHECK(strcmp(decoded.addr, "0xABCDE3") == 0);
    CHECK(strcmp(decoded.key, "3") == 0);
    CHECK(strcmp(decoded.source, "c") == 0);
    CHECK(decoded.confidence > 0.0f);

    frame.len = 49u;
    memset(&decoded, 0xA5, sizeof(decoded));
    CHECK(rf_decode_frame(&frame, &decoded) == RF_DECODE_RC_NO_FRAME);
    rf_decode_get_last_call_stats(&last);
    CHECK(last.rc == RF_DECODE_RC_NO_FRAME);
    CHECK(last.c_ok == 0);
    CHECK(last.c_confidence == 0.0f);
    CHECK(decoded.raw_code == 0u);

    rf_decode_get_runtime_stats(&runtime);
    CHECK(runtime.c_attempts == 2u);
    CHECK(runtime.c_accepts == 1u);
    return 0;
}

int main(void) {
    CHECK(test_protocol_encoder() == 0);
    CHECK(test_decoder_success_and_failure_stats() == 0);
    return 0;
}
