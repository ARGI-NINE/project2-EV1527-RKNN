#include "rf_decode_c.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define EV1527_BITS 24u
#define EV1527_FRAME_PULSES (2u + EV1527_BITS * 2u)
#define EV1527_SAMPLE_RATE 8000.0f
#define EV1527_CLK_MIN_US 230.0f
#define EV1527_CLK_MAX_US 4240.0f
#define EV1527_SYNC_LOW_LONGEST_MIN_RATIO 3.0f
#define EV1527_PROFILE_BIT_SHORT_T 1.0f
#define EV1527_PROFILE_BIT_LONG_T 3.0f
#define EV1527_PROFILE_SYNC_HIGH_T 1.0f
#define EV1527_PROFILE_SYNC_LOW_T 31.0f
#define EV1527_PROFILE_CLOCK_DIVISOR 1.0f
#define EV1527_SAMPLE_QUANT_TOL 2.0f

typedef struct {
    int level;
    uint16_t length;
} rf_run_t;

static uint16_t us_to_samples(uint16_t us_value) {
    const float samples = ((float)us_value * EV1527_SAMPLE_RATE) / 1000000.0f;
    const int rounded = (int)lroundf(samples);
    return (uint16_t)((rounded < 1) ? 1 : rounded);
}

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

static uint16_t upper_median_u16(const uint16_t *vals, size_t n) {
    uint16_t tmp[EV1527_BITS];
    size_t i = 0u;
    if (vals == NULL || n == 0u || n > EV1527_BITS) {
        return 0u;
    }
    for (i = 0u; i < n; ++i) {
        tmp[i] = vals[i];
    }
    qsort(tmp, n, sizeof(tmp[0]), cmp_u16);
    return tmp[n / 2u];
}

static float rel_err_quantized(float obs_len, float expected_len) {
    float diff = 0.0f;
    if (expected_len < 1.0f) {
        expected_len = 1.0f;
    }
    diff = fabsf(obs_len - expected_len) - EV1527_SAMPLE_QUANT_TOL;
    if (diff < 0.0f) {
        diff = 0.0f;
    }
    return diff / expected_len;
}

static void build_runs_from_frame(const rf_frame_t *frame, int start_level, rf_run_t *runs_out, uint16_t *run_count_out) {
    uint16_t i = 0u;
    int level = start_level ? 1 : 0;
    if (runs_out == NULL || run_count_out == NULL) {
        return;
    }
    *run_count_out = 0u;
    if (frame == NULL) {
        return;
    }
    for (i = 0u; i < frame->len && i < RF_BUFFER_SIZE; ++i) {
        const uint16_t sample_len = us_to_samples(frame->pulse[i]);
        runs_out[i].level = level;
        runs_out[i].length = sample_len;
        level = level ? 0 : 1;
        *run_count_out = (uint16_t)(*run_count_out + 1u);
    }
}

static int decode_best_from_runs(
    const rf_run_t *runs,
    uint16_t run_count,
    rf_decode_result_c_t *best_out,
    rf_decode_stage_stats_t *stats
) {
    const float min_clk = EV1527_SAMPLE_RATE * (EV1527_CLK_MIN_US * 1e-6f / EV1527_PROFILE_CLOCK_DIVISOR);
    const float max_clk = EV1527_SAMPLE_RATE * (EV1527_CLK_MAX_US * 1e-6f / EV1527_PROFILE_CLOCK_DIVISOR);
    const float pair_t = EV1527_PROFILE_BIT_SHORT_T + EV1527_PROFILE_BIT_LONG_T;
    int found = 0;
    float best_conf = -1.0f;
    uint16_t i = 0u;

    if (runs == NULL || best_out == NULL || run_count < EV1527_FRAME_PULSES) {
        return -1;
    }

    for (i = 1u; i < (uint16_t)(run_count - (uint16_t)(2u * EV1527_BITS)); ++i) {
        uint16_t totals[EV1527_BITS];
        uint16_t lows[EV1527_BITS];
        uint16_t sorted_bit_errs[EV1527_BITS];
        float sync_high = 0.0f;
        float sync_low = 0.0f;
        uint16_t bit_start = (uint16_t)(i + 1u);
        float clk_from_totals = 0.0f;
        float clk_from_sync = 0.0f;
        float clk = 0.0f;
        float sync_lo_ratio = 0.0f;
        float bit_err_sum = 0.0f;
        float bit_error = 0.0f;
        float max_bit_error = 0.0f;
        float period_base = 0.0f;
        float period_jitter = 0.0f;
        float period_spread = 0.0f;
        float sync_error = 0.0f;
        float low_ratio = 0.0f;
        float raw_conf = 0.0f;
        float conf = 0.0f;
        float min_run_t = 0.0f;
        float max_run_t = 0.0f;
        float min_total = 0.0f;
        float max_total = 0.0f;
        float avg_data_low = 0.0f;
        float p90_bit_error = 0.0f;
        uint16_t period_med = 0u;
        uint16_t period_outliers = 0u;
        uint16_t bad_count = 0u;
        uint16_t b = 0u;
        uint32_t code = 0u;

        if (runs[i - 1u].level != 1 || runs[i].level != 0) {
            continue;
        }

        sync_high = (float)runs[i - 1u].length;
        sync_low = (float)runs[i].length;
        if ((uint32_t)bit_start + (uint32_t)(2u * EV1527_BITS) > run_count) {
            continue;
        }

        {
            const float sync_low_min = fmaxf(45.0f, EV1527_PROFILE_SYNC_LOW_T * min_clk * 0.78f);
            const float sync_low_max = EV1527_PROFILE_SYNC_LOW_T * max_clk * 1.45f;
            if (sync_low < sync_low_min || sync_low > sync_low_max) {
                continue;
            }
            if (
                sync_high < EV1527_PROFILE_SYNC_HIGH_T * min_clk * 0.20f ||
                sync_high > EV1527_PROFILE_SYNC_HIGH_T * max_clk * 2.20f
            ) {
                continue;
            }
        }

        for (b = 0u; b < (uint16_t)(2u * EV1527_BITS); ++b) {
            const int expected_level = ((b & 1u) == 0u) ? 1 : 0;
            if (runs[bit_start + b].level != expected_level) {
                break;
            }
        }
        if (b != (uint16_t)(2u * EV1527_BITS)) {
            continue;
        }

        for (b = 0u; b < EV1527_BITS; ++b) {
            totals[b] = (uint16_t)(runs[bit_start + 2u * b].length + runs[bit_start + 2u * b + 1u].length);
            lows[b] = runs[bit_start + 2u * b + 1u].length;
            avg_data_low += (float)lows[b];
        }
        avg_data_low /= (float)EV1527_BITS;
        {
            uint16_t max_data_low = 0u;
            for (b = 0u; b < EV1527_BITS; ++b) {
                if (lows[b] > max_data_low) {
                    max_data_low = lows[b];
                }
            }
            if (sync_low <= (float)max_data_low) {
                continue;
            }
            if ((sync_low / fmaxf(1.0f, (float)max_data_low)) < EV1527_SYNC_LOW_LONGEST_MIN_RATIO) {
                continue;
            }
        }

        if (stats != NULL) {
            stats->step1_structural++;
        }

        clk_from_totals = (float)upper_median_u16(totals, EV1527_BITS) / pair_t;
        clk_from_sync = sync_low / EV1527_PROFILE_SYNC_LOW_T;
        clk = 0.80f * clk_from_totals + 0.20f * clk_from_sync;
        if (clk < min_clk || clk > max_clk) {
            continue;
        }

        sync_lo_ratio = sync_low / fmaxf(1e-9f, clk);
        if (
            sync_lo_ratio < EV1527_PROFILE_SYNC_LOW_T * 0.55f ||
            sync_lo_ratio > EV1527_PROFILE_SYNC_LOW_T * 1.75f
        ) {
            continue;
        }

        for (b = 0u; b < EV1527_BITS; ++b) {
            const float hi = (float)runs[bit_start + 2u * b].length;
            const float lo = (float)runs[bit_start + 2u * b + 1u].length;
            const float err0 =
                rel_err_quantized(hi, EV1527_PROFILE_BIT_SHORT_T * clk) +
                rel_err_quantized(lo, EV1527_PROFILE_BIT_LONG_T * clk);
            const float err1 =
                rel_err_quantized(hi, EV1527_PROFILE_BIT_LONG_T * clk) +
                rel_err_quantized(lo, EV1527_PROFILE_BIT_SHORT_T * clk);
            const float best_err = (err1 < err0) ? err1 : err0;

            if (err1 < err0) {
                code = (code << 1u) | 1u;
            } else {
                code = (code << 1u);
            }
            bit_err_sum += best_err;
            sorted_bit_errs[b] = (uint16_t)lroundf(best_err * 1000.0f);
            if (best_err > max_bit_error) {
                max_bit_error = best_err;
            }
        }

        bit_error = bit_err_sum / (float)EV1527_BITS;
        qsort(sorted_bit_errs, EV1527_BITS, sizeof(sorted_bit_errs[0]), cmp_u16);
        p90_bit_error = ((float)sorted_bit_errs[(int)(0.90f * (EV1527_BITS - 1u))]) / 1000.0f;
        if (bit_error > 3.00f || p90_bit_error > 5.00f || max_bit_error > 12.00f) {
            continue;
        }

        period_base = pair_t * clk;
        min_total = (float)totals[0];
        max_total = (float)totals[0];
        period_med = upper_median_u16(totals, EV1527_BITS);
        for (b = 0u; b < EV1527_BITS; ++b) {
            const float total = (float)totals[b];
            period_jitter += rel_err_quantized(total, period_base);
            if (total < min_total) {
                min_total = total;
            }
            if (total > max_total) {
                max_total = total;
            }
            if (fabsf(total - (float)period_med) > (0.45f * (float)period_med + EV1527_SAMPLE_QUANT_TOL)) {
                period_outliers++;
            }
        }
        period_jitter /= (float)EV1527_BITS;
        period_spread = (max_total - min_total) / fmaxf(1e-9f, period_base);
        sync_error =
            rel_err_quantized(sync_high, EV1527_PROFILE_SYNC_HIGH_T * clk) +
            rel_err_quantized(sync_low, EV1527_PROFILE_SYNC_LOW_T * clk);

        if (period_jitter > 1.80f || period_spread > 5.00f || period_outliers > 8u || sync_error > 8.50f) {
            continue;
        }

        low_ratio = sync_low / fmaxf(1e-6f, avg_data_low);
        if (low_ratio < 2.20f) {
            continue;
        }

        min_run_t = EV1527_PROFILE_BIT_SHORT_T * clk;
        max_run_t = EV1527_PROFILE_SYNC_LOW_T * clk;
        for (b = (uint16_t)(i - 1u); b < (uint16_t)(bit_start + 2u * EV1527_BITS); ++b) {
            const float run_len = (float)runs[b].length;
            if (run_len < (min_run_t - EV1527_SAMPLE_QUANT_TOL)) {
                bad_count++;
            }
            if (run_len > (max_run_t + EV1527_SAMPLE_QUANT_TOL)) {
                bad_count++;
            }
        }
        if (bad_count > 10u) {
            continue;
        }

        {
            const float bit_norm = fminf(bit_error / 3.00f, 1.0f);
            const float jitter_norm = fminf(period_jitter / 1.80f, 1.0f);
            const float spread_norm = fminf(period_spread / 5.00f, 1.0f);
            const float outlier_norm = fminf((float)period_outliers / 10.0f, 1.0f);
            const float sync_error_norm = fminf(sync_error / 8.50f, 1.0f);
            const float sync_ratio_penalty = fminf(fabsf(sync_lo_ratio - EV1527_PROFILE_SYNC_LOW_T) / EV1527_PROFILE_SYNC_LOW_T, 1.0f);
            const float low_penalty = fmaxf(0.0f, (1.8f - low_ratio) / 1.8f);
            raw_conf = 1.0f - (
                0.41f * bit_norm +
                0.19f * jitter_norm +
                0.14f * spread_norm +
                0.03f * outlier_norm +
                0.08f * sync_error_norm +
                0.05f * sync_ratio_penalty +
                0.10f * low_penalty
            );
            conf = fmaxf(0.0f, raw_conf) * fminf(1.0f, low_ratio / 2.2f);
        }

        if (stats != NULL) {
            stats->step2_timing++;
        }

        if (
            !found ||
            conf > best_conf ||
            (fabsf(conf - best_conf) <= 1e-6f && bit_error < best_out->bit_error) ||
            (
                fabsf(conf - best_conf) <= 1e-6f &&
                fabsf(bit_error - best_out->bit_error) <= 1e-6f &&
                period_jitter < best_out->period_jitter
            )
        ) {
            best_conf = conf;
            best_out->raw_code = code & 0xFFFFFFu;
            best_out->address20 = (code >> 4u) & 0xFFFFFu;
            best_out->button4 = (uint8_t)(code & 0x0Fu);
            best_out->clk_us = (clk * 1000000.0f / EV1527_SAMPLE_RATE) * EV1527_PROFILE_CLOCK_DIVISOR;
            best_out->confidence = conf;
            best_out->bit_error = bit_error;
            best_out->sync_error = sync_error;
            best_out->period_jitter = period_jitter;
            best_out->start_index = (uint16_t)(i - 1u);
            found = 1;
        }
    }

    return found ? 0 : -3;
}

int rf_decode_ev1527_c(const rf_frame_t *frame, rf_decode_result_c_t *out) {
    return rf_decode_ev1527_c_with_stats(frame, out, NULL);
}

int rf_decode_ev1527_c_with_stats(const rf_frame_t *frame, rf_decode_result_c_t *out, rf_decode_stage_stats_t *stats) {
    rf_run_t runs[RF_BUFFER_SIZE];
    uint16_t run_count = 0u;
    rf_decode_result_c_t best;
    int found = 0;
    int phase = 0;

    if (stats != NULL) {
        memset(stats, 0, sizeof(*stats));
    }
    if (frame == NULL || out == NULL) {
        return -1;
    }
    if (frame->len < EV1527_FRAME_PULSES) {
        return -2;
    }

    memset(&best, 0, sizeof(best));
    best.confidence = -1.0f;

    for (phase = 1; phase >= 0; --phase) {
        rf_decode_result_c_t phase_best;
        memset(&phase_best, 0, sizeof(phase_best));
        phase_best.confidence = -1.0f;
        build_runs_from_frame(frame, phase, runs, &run_count);
        if (decode_best_from_runs(runs, run_count, &phase_best, stats) != 0) {
            continue;
        }
        if (
            !found ||
            phase_best.confidence > best.confidence ||
            (fabsf(phase_best.confidence - best.confidence) <= 1e-6f && phase_best.bit_error < best.bit_error) ||
            (
                fabsf(phase_best.confidence - best.confidence) <= 1e-6f &&
                fabsf(phase_best.bit_error - best.bit_error) <= 1e-6f &&
                phase_best.period_jitter < best.period_jitter
            )
        ) {
            best = phase_best;
            found = 1;
        }
    }

    if (!found) {
        return -3;
    }

    *out = best;
    return 0;
}
