#ifndef RF_MQTT_H
#define RF_MQTT_H

#include "rf_decode.h"

typedef struct {
    char device[64];
    char action[16];
} rf_mqtt_cmd_t;

typedef struct {
    int read_fd;
    int write_fd;
} rf_mqtt_client_t;

int rf_mqtt_init(rf_mqtt_client_t *client);
int rf_mqtt_fd(const rf_mqtt_client_t *client);
int rf_mqtt_inject_command(rf_mqtt_client_t *client, const char *json_text);
int rf_mqtt_read_command(rf_mqtt_client_t *client, rf_mqtt_cmd_t *cmd);
int rf_mqtt_publish_decode(const rf_decoded_packet_t *pkt);
void rf_mqtt_close(rf_mqtt_client_t *client);

#endif

