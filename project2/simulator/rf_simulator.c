#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <time.h>
#endif

#include "../common/rf_protocol.h"

#define SIM_MAX_FRAMES 256u

typedef struct {
    rf_frame_t frames[SIM_MAX_FRAMES];
    size_t count;
} sim_store_t;

static sim_store_t g_store;
static FILE *g_out;
static unsigned g_interval_ms = 80u;

static void sleep_ms(unsigned ms) {
#if defined(_WIN32)
    Sleep(ms);
#else
    struct timespec ts;
    ts.tv_sec = (time_t)(ms / 1000u);
    ts.tv_nsec = (long)((ms % 1000u) * 1000000u);
    nanosleep(&ts, NULL);
#endif
}

static int push_frame_from_current(rf_frame_t *cur, sim_store_t *store) {
    if (cur->len == 0u) {
        return 0;
    }
    if (store->count >= SIM_MAX_FRAMES) {
        return -1;
    }
    store->frames[store->count++] = *cur;
    cur->len = 0u;
    return 0;
}

void sim_load_wave(char *file) {
    FILE *fp = NULL;
    char line[256];
    rf_frame_t cur;
    memset(&g_store, 0, sizeof(g_store));
    memset(&cur, 0, sizeof(cur));

    if (file == NULL) {
        return;
    }

    fp = fopen(file, "rb");
    if (fp == NULL) {
        return;
    }

    while (fgets(line, sizeof(line), fp) != NULL) {
        char *p = line;
        while (*p != '\0' && isspace((unsigned char)*p)) {
            ++p;
        }
        if (*p == '\0' || *p == '#') {
            (void)push_frame_from_current(&cur, &g_store);
            continue;
        }
        {
            long v = strtol(p, NULL, 10);
            if (v <= 0 || v > 65535L) {
                continue;
            }
            if (cur.len < RF_BUFFER_SIZE) {
                cur.pulse[cur.len++] = (uint16_t)v;
            }
        }
    }
    (void)push_frame_from_current(&cur, &g_store);
    fclose(fp);
}

void sim_send_frame(void) {
    size_t i = 0u;
    uint8_t packet[2u + 2u + RF_BUFFER_SIZE * 2u + 1u];
    if (g_out == NULL) {
        return;
    }
    for (i = 0u; i < g_store.count; ++i) {
        const size_t n = rf_proto_encode(&g_store.frames[i], packet, sizeof(packet));
        if (n > 0u) {
            fwrite(packet, 1u, n, g_out);
            fflush(g_out);
            sleep_ms(g_interval_ms);
        }
    }
}

static void print_usage(const char *exe) {
    printf("Usage: %s --pulse-file <path> [--output <path|->] [--repeat N] [--interval-ms N]\n", exe);
    printf("  pulse.txt format: one pulse duration(us) per line, blank line splits frames.\n");
}

int main(int argc, char **argv) {
    const char *pulse_file = NULL;
    const char *output = "-";
    unsigned repeat = 1u;
    unsigned r = 0u;
    int i = 0;

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--pulse-file") == 0 && i + 1 < argc) {
            pulse_file = argv[++i];
        } else if (strcmp(argv[i], "--output") == 0 && i + 1 < argc) {
            output = argv[++i];
        } else if (strcmp(argv[i], "--repeat") == 0 && i + 1 < argc) {
            repeat = (unsigned)strtoul(argv[++i], NULL, 10);
            if (repeat == 0u) {
                repeat = 1u;
            }
        } else if (strcmp(argv[i], "--interval-ms") == 0 && i + 1 < argc) {
            g_interval_ms = (unsigned)strtoul(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else {
            print_usage(argv[0]);
            return 1;
        }
    }

    if (pulse_file == NULL) {
        print_usage(argv[0]);
        return 2;
    }

    sim_load_wave((char *)pulse_file);
    if (g_store.count == 0u) {
        printf("No frames loaded from %s\n", pulse_file);
        return 3;
    }

    if (strcmp(output, "-") == 0) {
        g_out = stdout;
    } else {
        g_out = fopen(output, "wb");
        if (g_out == NULL) {
            printf("Open output failed: %s\n", output);
            return 4;
        }
    }

    printf(
        "rf_simulator: frames=%u repeat=%u interval_ms=%u output=%s\n",
        (unsigned)g_store.count,
        repeat,
        g_interval_ms,
        output
    );

    for (r = 0u; r < repeat; ++r) {
        sim_send_frame();
    }

    if (g_out != stdout) {
        fclose(g_out);
    }
    return 0;
}

