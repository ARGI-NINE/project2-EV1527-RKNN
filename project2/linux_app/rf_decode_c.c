#include "rf_decode_c.h"

#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define EV1527_BITS 24u
#define EV1527_FRAME_PULSES (2u + EV1527_BITS * 2u)

static int cmp_u16(const void *a, const void *b) {
    const uint16_t va = *(const uint16_t *)a;
    const uint16_t vb = *(const uint16_t *)b;
    if (va < vb) {
        return -1;
    }
    if (va > vb) {
        return 1;
    }
    return 0;
}

static float median_u16(const uint16_t *vals, size_t n) {
    uint16_t tmp[EV1527_BITS];
    size_t i = 0u;
    if (vals == NULL || n == 0u || n > EV1527_BITS) {
        return 0.0f;
    }
    for (i = 0u; i < n; ++i) {
        tmp[i] = vals[i];
    }
    qsort(tmp, n, sizeof(tmp[0]), cmp_u16);
    if ((n & 1u) != 0u) {
        return (float)tmp[n / 2u];
    }
    return ((float)tmp[n / 2u - 1u] + (float)tmp[n / 2u]) * 0.5f;
}

static float rel_err(float obs, float exp) {
    if (exp < 1.0f) {
        exp = 1.0f;
    }
    return fabsf(obs - exp) / exp;
}

int rf_decode_ev1527_c(const rf_frame_t *frame, rf_decode_result_c_t *out) {
    uint16_t start = 0u;
    float best_score = 0.0f;
    rf_decode_result_c_t best;
    int found = 0;

    if (frame == NULL || out == NULL) {
        return -1;
    }
    if (frame->len < EV1527_FRAME_PULSES) {
        return -2;
    }

    memset(&best, 0, sizeof(best));

    for (start = 0u; start + EV1527_FRAME_PULSES <= frame->len; ++start) {
        uint16_t pair_totals[EV1527_BITS];
        float t_us = 0.0f;
        float sync_hi = 0.0f;
        float sync_lo = 0.0f;
        float sync_err = 0.0f;
        float bit_err_sum = 0.0f;
        float score = 0.0f;
        uint32_t code = 0u;
        uint16_t i = 0u;

        sync_hi = (float)frame->pulse[start];
        sync_lo = (float)frame->pulse[start + 1u];
        if (sync_lo < 8000.0f || sync_hi < 100.0f) {
            continue;
        }

        for (i = 0u; i < EV1527_BITS; ++i) {
            const uint16_t hi = frame->pulse[start + 2u + i * 2u];
            const uint16_t lo = frame->pulse[start + 2u + i * 2u + 1u];
            pair_totals[i] = (uint16_t)(hi + lo);
        }

        t_us = median_u16(pair_totals, EV1527_BITS) / 16.0f;
        if (t_us < 120.0f || t_us > 1200.0f) {
            continue;
        }

        sync_err = rel_err(sync_hi, 4.0f * t_us) + rel_err(sync_lo, 124.0f * t_us);
        if (sync_err > 2.2f) {
            continue;
        }

        for (i = 0u; i < EV1527_BITS; ++i) {
            const float hi = (float)frame->pulse[start + 2u + i * 2u];
            const float lo = (float)frame->pulse[start + 2u + i * 2u + 1u];
            const float err0 = rel_err(hi, 4.0f * t_us) + rel_err(lo, 12.0f * t_us);
            const float err1 = rel_err(hi, 12.0f * t_us) + rel_err(lo, 4.0f * t_us);
            const int bit = (err1 < err0) ? 1 : 0;
            bit_err_sum += (err1 < err0) ? err1 : err0;
            code = (code << 1u) | (uint32_t)bit;
        }

        score = 1.0f - fminf(1.0f, (bit_err_sum / (float)EV1527_BITS + sync_err * 0.5f) / 2.5f);
        if (score > best_score) {
            best_score = score;
            best.raw_code = code;
            best.address20 = (code >> 4u) & 0xFFFFFu;
            best.button4 = (uint8_t)(code & 0x0Fu);
            best.clk_us = t_us;
            best.confidence = score;
            found = 1;
        }
    }

    if (!found || best_score < 0.45f) {
        return -3;
    }

    *out = best;
    return 0;
}
