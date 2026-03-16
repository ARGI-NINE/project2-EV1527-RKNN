#include "rf_mqtt.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#if defined(__linux__) || defined(__APPLE__)
#include <sys/socket.h>
#include <unistd.h>
#else
#include <io.h>
#include <fcntl.h>
#endif

static int extract_value(const char *json, const char *key, char *out, size_t out_cap) {
    char needle[64];
    const char *p = NULL;
    const char *q = NULL;
    size_t n = 0u;
    if (json == NULL || key == NULL || out == NULL || out_cap == 0u) {
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
    if (n >= out_cap) {
        n = out_cap - 1u;
    }
    memcpy(out, p, n);
    out[n] = '\0';
    return 0;
}

int rf_mqtt_init(rf_mqtt_client_t *client) {
    if (client == NULL) {
        return -1;
    }
    client->read_fd = -1;
    client->write_fd = -1;

#if defined(__linux__) || defined(__APPLE__)
    {
        int fdv[2] = {-1, -1};
        if (socketpair(AF_UNIX, SOCK_DGRAM, 0, fdv) != 0) {
            return -2;
        }
        client->read_fd = fdv[0];
        client->write_fd = fdv[1];
    }
#else
    {
        int fdv[2] = {-1, -1};
        if (_pipe(fdv, 1024, _O_BINARY) != 0) {
            return -2;
        }
        client->read_fd = fdv[0];
        client->write_fd = fdv[1];
    }
#endif
    return 0;
}

int rf_mqtt_fd(const rf_mqtt_client_t *client) {
    if (client == NULL) {
        return -1;
    }
    return client->read_fd;
}

int rf_mqtt_inject_command(rf_mqtt_client_t *client, const char *json_text) {
    size_t len = 0u;
    if (client == NULL || client->write_fd < 0 || json_text == NULL) {
        return -1;
    }
    len = strlen(json_text);
#if defined(__linux__) || defined(__APPLE__)
    if (write(client->write_fd, json_text, len) < 0) {
        return -2;
    }
#else
    if (_write(client->write_fd, json_text, (unsigned)len) < 0) {
        return -2;
    }
#endif
    return 0;
}

int rf_mqtt_read_command(rf_mqtt_client_t *client, rf_mqtt_cmd_t *cmd) {
    char buf[256];
    int n = 0;
    if (client == NULL || client->read_fd < 0 || cmd == NULL) {
        return -1;
    }

    memset(cmd, 0, sizeof(*cmd));
#if defined(__linux__) || defined(__APPLE__)
    n = (int)read(client->read_fd, buf, sizeof(buf) - 1u);
#else
    n = _read(client->read_fd, buf, (unsigned)(sizeof(buf) - 1u));
#endif
    if (n <= 0) {
        return -2;
    }
    buf[n] = '\0';
    if (extract_value(buf, "device", cmd->device, sizeof(cmd->device)) != 0) {
        return -3;
    }
    if (extract_value(buf, "action", cmd->action, sizeof(cmd->action)) != 0) {
        return -4;
    }
    return 0;
}

int rf_mqtt_publish_decode(const rf_decoded_packet_t *pkt) {
    if (pkt == NULL) {
        return -1;
    }
    printf(
        "[MQTT PUB] topic=home/rf433/report payload={\"addr\":\"%s\",\"key\":\"%s\",\"conf\":%.2f,\"src\":\"%s\"}\n",
        pkt->addr,
        pkt->key,
        pkt->confidence,
        pkt->source
    );
    return 0;
}

void rf_mqtt_close(rf_mqtt_client_t *client) {
    if (client == NULL) {
        return;
    }
#if defined(__linux__) || defined(__APPLE__)
    if (client->read_fd >= 0) {
        close(client->read_fd);
    }
    if (client->write_fd >= 0) {
        close(client->write_fd);
    }
#else
    if (client->read_fd >= 0) {
        _close(client->read_fd);
    }
    if (client->write_fd >= 0) {
        _close(client->write_fd);
    }
#endif
    client->read_fd = -1;
    client->write_fd = -1;
}

