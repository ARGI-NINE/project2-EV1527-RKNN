#include "rf_decode_window.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    unsigned code;
    char key[8];
    float conf_sum;
    uint16_t hits;
} rf_vote_t;

static const rf_frame_t *win_at(const rf_decode_window_t *win, uint16_t idx) {
    uint16_t pos = 0u;
    if (win == NULL || idx >= win->count) {
        return NULL;
    }
    pos = (uint16_t)((win->head + win->capacity - win->count + idx) % win->capacity);
    return &win->frames[pos];
}

void rf_decode_window_init(
    rf_decode_window_t *win,
    uint16_t capacity,
    uint16_t stable_repeat,
    float min_confidence
) {
    if (win == NULL) {
        return;
    }
    memset(win, 0, sizeof(*win));
    if (capacity == 0u || capacity > RF_DECODE_WINDOW_MAX) {
        capacity = 12u;
    }
    if (stable_repeat == 0u) {
        stable_repeat = 3u;
    }
    win->capacity = capacity;
    win->stable_repeat = stable_repeat;
    win->min_confidence = min_confidence;
}

void rf_decode_window_push(rf_decode_window_t *win, const rf_frame_t *frame) {
    if (win == NULL || frame == NULL || win->capacity == 0u) {
        return;
    }
    win->frames[win->head] = *frame;
    win->head = (uint16_t)((win->head + 1u) % win->capacity);
    if (win->count < win->capacity) {
        win->count++;
    }
}

int rf_decode_window_try_decode(
    rf_decode_window_t *win,
    const char *python_bin,
    const char *python_script,
    rf_decoded_packet_t *out,
    uint16_t *agree_count
) {
    rf_vote_t votes[RF_DECODE_WINDOW_MAX];
    uint16_t vote_n = 0u;
    uint16_t i = 0u;
    int best = -1;

    (void)python_bin;
    (void)python_script;

    if (agree_count != NULL) {
        *agree_count = 0u;
    }
    if (win == NULL || out == NULL || win->count == 0u) {
        return -1;
    }

    memset(votes, 0, sizeof(votes));
    for (i = 0u; i < win->count; ++i) {
        rf_decoded_packet_t pkt;
        const rf_frame_t *fr = win_at(win, i);
        uint16_t k = 0u;
        int found = 0;

        if (fr == NULL) {
            continue;
        }
        if (rf_decode_frame_c(fr, &pkt) != 0) {
            continue;
        }
        if (pkt.confidence < win->min_confidence) {
            continue;
        }

        for (k = 0u; k < vote_n; ++k) {
            if (votes[k].code == pkt.raw_code) {
                votes[k].hits++;
                votes[k].conf_sum += pkt.confidence;
                found = 1;
                break;
            }
        }
        if (!found && vote_n < RF_DECODE_WINDOW_MAX) {
            votes[vote_n].code = pkt.raw_code;
            snprintf(votes[vote_n].key, sizeof(votes[vote_n].key), "%s", pkt.key);
            votes[vote_n].hits = 1u;
            votes[vote_n].conf_sum = pkt.confidence;
            vote_n++;
        }
    }

    for (i = 0u; i < vote_n; ++i) {
        if (best < 0) {
            best = (int)i;
            continue;
        }
        if (votes[i].hits > votes[best].hits) {
            best = (int)i;
        } else if (votes[i].hits == votes[best].hits && votes[i].conf_sum > votes[best].conf_sum) {
            best = (int)i;
        }
    }

    if (best < 0) {
        return -2;
    }
    if (votes[best].hits < win->stable_repeat) {
        return -3;
    }

    memset(out, 0, sizeof(*out));
    out->raw_code = votes[best].code;
    snprintf(out->addr, sizeof(out->addr), "0x%06X", votes[best].code & 0xFFFFFFu);
    snprintf(out->key, sizeof(out->key), "%s", votes[best].key);
    out->confidence = votes[best].conf_sum / (float)votes[best].hits;
    snprintf(out->source, sizeof(out->source), "c-win");
    if (agree_count != NULL) {
        *agree_count = votes[best].hits;
    }
    return 0;
}
