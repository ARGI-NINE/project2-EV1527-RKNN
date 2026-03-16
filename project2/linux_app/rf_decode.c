#include "rf_decode.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !defined(_WIN32)
#include <unistd.h>
#endif

static int rf_decode_make_temp_path(char *out_path, size_t cap) {
    if (out_path == NULL || cap == 0u) {
        return -1;
    }

#if defined(_WIN32)
    {
        const char *tmp_dir = getenv("TEMP");
        char templ[260];
        int n = 0;
        if (tmp_dir == NULL || tmp_dir[0] == '\0') {
            tmp_dir = ".";
        }
        n = snprintf(templ, sizeof(templ), "%s\\rf_pulse_XXXXXX", tmp_dir);
        if (n <= 0 || (size_t)n >= sizeof(templ)) {
            return -2;
        }
        if (_mktemp_s(templ, sizeof(templ)) != 0) {
            return -3;
        }
        n = snprintf(out_path, cap, "%s", templ);
        if (n <= 0 || (size_t)n >= cap) {
            return -4;
        }
    }
#else
    {
        const char *tmp_dir = getenv("TMPDIR");
        int fd = -1;
        int n = 0;
        if (tmp_dir == NULL || tmp_dir[0] == '\0') {
            tmp_dir = "/tmp";
        }
        n = snprintf(out_path, cap, "%s/rf_pulse_XXXXXX", tmp_dir);
        if (n <= 0 || (size_t)n >= cap) {
            return -2;
        }
        fd = mkstemp(out_path);
        if (fd < 0) {
            return -3;
        }
        close(fd);
    }
#endif
    return 0;
}

static int rf_decode_call_python_ex(
    const char *python_bin,
    const char *python_script,
    const char *pulse_file,
    char *result,
    size_t result_cap
) {
    char cmd[1024];
    FILE *pipe = NULL;

    if (pulse_file == NULL || result == NULL || result_cap == 0u) {
        return -1;
    }

    if (python_bin == NULL || python_bin[0] == '\0') {
        python_bin = "python3";
    }
    if (python_script == NULL || python_script[0] == '\0') {
        python_script = "./python/ev1527_decode_bridge.py";
    }

    result[0] = '\0';
    snprintf(cmd, sizeof(cmd), "\"%s\" \"%s\" \"%s\"", python_bin, python_script, pulse_file);
    pipe = popen(cmd, "r");
    if (pipe == NULL) {
        return -2;
    }

    if (fgets(result, (int)result_cap, pipe) == NULL) {
        (void)pclose(pipe);
        return -3;
    }
    (void)pclose(pipe);
    return 0;
}

int rf_decode_call_python(char *pulse_file, char *result) {
    const char *python_bin = getenv("RF_PYTHON_BIN");
    const char *python_script = getenv("RF_PY_DECODER");
    return rf_decode_call_python_ex(python_bin, python_script, pulse_file, result, 512u);
}

int rf_decode_write_pulse_file(const rf_frame_t *frame, const char *path) {
    FILE *fp = NULL;
    uint16_t i = 0u;

    if (frame == NULL || path == NULL) {
        return -1;
    }

    fp = fopen(path, "wb");
    if (fp == NULL) {
        return -2;
    }

    fprintf(fp, "{\"pulse\":[");
    for (i = 0u; i < frame->len; ++i) {
        fprintf(fp, "%u", frame->pulse[i]);
        if (i + 1u < frame->len) {
            fputc(',', fp);
        }
    }
    fprintf(fp, "]}\n");

    fclose(fp);
    return 0;
}

static int json_extract_str(const char *json, const char *key, char *out, size_t cap) {
    char needle[64];
    const char *p = NULL;
    const char *q = NULL;
    size_t n = 0u;

    if (json == NULL || key == NULL || out == NULL || cap == 0u) {
        return -1;
    }

    snprintf(needle, sizeof(needle), "\"%s\"", key);
    p = strstr(json, needle);
    if (p == NULL) {
        return -2;
    }
    p = strchr(p, ':');
    if (p == NULL) {
        return -3;
    }
    ++p;
    while (*p != '\0' && isspace((unsigned char)*p)) {
        ++p;
    }
    if (*p == '"') {
        ++p;
    }
    q = p;
    while (*q != '\0' && *q != '"' && *q != ',' && *q != '}') {
        ++q;
    }

    n = (size_t)(q - p);
    if (n >= cap) {
        n = cap - 1u;
    }
    memcpy(out, p, n);
    out[n] = '\0';
    return 0;
}

int rf_decode_frame(
    const rf_frame_t *frame,
    const char *python_bin,
    const char *python_script,
    rf_decoded_packet_t *out
) {
    int rc = 0;

    if (frame == NULL || out == NULL) {
        return -1;
    }

    memset(out, 0, sizeof(*out));
    rc = rf_decode_frame_c(frame, out);
    if (rc == 0) {
        return 0;
    }

    if (python_script != NULL && python_script[0] != '\0') {
        char temp_name[260];
        char py_out[512];
        int py_rc = 0;
        if (rf_decode_make_temp_path(temp_name, sizeof(temp_name)) != 0) {
            return -2;
        }
        if (rf_decode_write_pulse_file(frame, temp_name) != 0) {
            remove(temp_name);
            return -3;
        }
        py_rc = rf_decode_call_python_ex(
            python_bin != NULL ? python_bin : getenv("RF_PYTHON_BIN"),
            python_script,
            temp_name,
            py_out,
            sizeof(py_out)
        );
        remove(temp_name);
        if (py_rc != 0) {
            return -4;
        }

        if (json_extract_str(py_out, "addr", out->addr, sizeof(out->addr)) != 0) {
            return -5;
        }
        if (json_extract_str(py_out, "key", out->key, sizeof(out->key)) != 0) {
            return -6;
        }
        out->confidence = 0.80f;
        snprintf(out->source, sizeof(out->source), "python");
        return 0;
    }

    return -7;
}

int rf_decode_frame_c(const rf_frame_t *frame, rf_decoded_packet_t *out) {
    rf_decode_result_c_t c_result;
    int rc = 0;
    if (frame == NULL || out == NULL) {
        return -1;
    }
    rc = rf_decode_ev1527_c(frame, &c_result);
    if (rc != 0) {
        return -2;
    }
    memset(out, 0, sizeof(*out));
    snprintf(out->addr, sizeof(out->addr), "0x%06X", c_result.raw_code);
    snprintf(out->key, sizeof(out->key), "%u", (unsigned)c_result.button4);
    snprintf(out->source, sizeof(out->source), "c");
    out->raw_code = c_result.raw_code;
    out->confidence = c_result.confidence;
    return 0;
}
