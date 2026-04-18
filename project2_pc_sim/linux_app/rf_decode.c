#include "rf_decode.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <time.h>
#endif

static rf_decode_runtime_stats_t g_rf_decode_stats;
static rf_decode_last_call_stats_t g_rf_last_call_stats;

static unsigned long long rf_now_us(void) {
#if defined(_WIN32)
    static LARGE_INTEGER freq;
    static int freq_ready = 0;
    LARGE_INTEGER counter;
    if (!freq_ready) {
        if (QueryPerformanceFrequency(&freq) == 0) {
            return (unsigned long long)GetTickCount() * 1000ull;
        }
        freq_ready = 1;
    }
    if (QueryPerformanceCounter(&counter) == 0 || freq.QuadPart <= 0) {
        return (unsigned long long)GetTickCount() * 1000ull;
    }
    return (unsigned long long)((counter.QuadPart * 1000000ll) / freq.QuadPart);
#else
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0u;
    }
    return (unsigned long long)ts.tv_sec * 1000000ull + (unsigned long long)(ts.tv_nsec / 1000ull);
#endif
}

int rf_decode_frame(
    const rf_frame_t *frame,
    rf_decoded_packet_t *out
) {
    unsigned long long t0, t1;
    rf_decode_result_c_t c_result;
    int c_rc;

    if (frame == NULL || out == NULL) {
        return -1;
    }

    memset(out, 0, sizeof(*out));
    memset(&g_rf_last_call_stats, 0, sizeof(g_rf_last_call_stats));
    g_rf_last_call_stats.frame_len = frame->len;

    t0 = rf_now_us();
    g_rf_decode_stats.c_attempts++;
    c_rc = rf_decode_ev1527_c_with_stats(frame, &c_result, NULL);
    t1 = rf_now_us();

    g_rf_last_call_stats.c_total_us = t1 - t0;
    g_rf_last_call_stats.total_us = t1 - t0;
    g_rf_last_call_stats.c_confidence = c_result.confidence;
    g_rf_decode_stats.c_total_us += g_rf_last_call_stats.c_total_us;

    if (c_rc == 0) {
        snprintf(out->addr, sizeof(out->addr), "0x%06X", c_result.raw_code);
        snprintf(out->key, sizeof(out->key), "%u", (unsigned)c_result.button4);
        snprintf(out->source, sizeof(out->source), "c");
        out->raw_code = c_result.raw_code;
        out->confidence = c_result.confidence;
        g_rf_last_call_stats.rc = 0;
        g_rf_last_call_stats.c_ok = 1;
        g_rf_decode_stats.c_accepts++;
        g_rf_decode_stats.c_accept_total_us += g_rf_last_call_stats.c_total_us;
        return 0;
    }

    g_rf_last_call_stats.rc = RF_DECODE_RC_NO_FRAME;
    return RF_DECODE_RC_NO_FRAME;
}

void rf_decode_get_runtime_stats(rf_decode_runtime_stats_t *out) {
    if (out == NULL) {
        return;
    }
    *out = g_rf_decode_stats;
}

void rf_decode_get_last_call_stats(rf_decode_last_call_stats_t *out) {
    if (out == NULL) {
        return;
    }
    *out = g_rf_last_call_stats;
}
