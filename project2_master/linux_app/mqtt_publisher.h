#ifndef MQTT_PUBLISHER_H
#define MQTT_PUBLISHER_H

#include <stdatomic.h>

struct mosquitto;

#define MQTT_PUBLISHER_ERR_INVALID (-1)
#define MQTT_PUBLISHER_ERR_NO_CONN (-2)
#define MQTT_PUBLISHER_ERR_TOPIC_TOO_LONG (-3)

typedef struct {
    struct mosquitto *mosq;
    atomic_int connected;
    int loop_started;
    int lib_initialized;
    char client_id[64];
    char host[64];
    int port;
    char topic_root[128];
} mqtt_publisher_t;

int mqtt_publisher_init(
    mqtt_publisher_t *publisher,
    const char *client_id,
    const char *host,
    int port,
    const char *topic_root
);
void mqtt_publisher_cleanup(mqtt_publisher_t *publisher);
int mqtt_publisher_publish(
    mqtt_publisher_t *publisher,
    const char *subtopic,
    const char *payload,
    int retain
);
int mqtt_publisher_is_connected(const mqtt_publisher_t *publisher);
const char *mqtt_publisher_error_string(int rc);

#endif
