#ifndef RF_DECODE_H
#define RF_DECODE_H

#include <stddef.h>

#include "../common/rf_protocol.h"
#include "rf_decode_c.h"

typedef struct {
    char addr[16];
    char key[8];
    float confidence;
    unsigned raw_code;
    char source[16];
} rf_decoded_packet_t;

int rf_decode_call_python(char *pulse_file, char *result);
int rf_decode_write_pulse_file(const rf_frame_t *frame, const char *path);
int rf_decode_frame_c(const rf_frame_t *frame, rf_decoded_packet_t *out);
int rf_decode_frame(
    const rf_frame_t *frame,
    const char *python_bin,
    const char *python_script,
    rf_decoded_packet_t *out
);

#endif
