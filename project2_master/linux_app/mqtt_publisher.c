#include "mqtt_publisher.h"

#include <mosquitto.h>

#include <stdio.h>
#include <string.h>

static void on_connect(struct mosquitto *mosq, void *userdata, int rc) {
    mqtt_publisher_t *publisher = (mqtt_publisher_t *)userdata;
    (void)mosq;
    if (publisher == NULL) {
        return;
    }
    atomic_store_explicit(&publisher->connected, rc == 0, memory_order_relaxed);
    if (rc != 0) {
        fprintf(stderr, "[MQTT] connect failed: %s\n", mosquitto_connack_string(rc));
    }
}

static void on_disconnect(struct mosquitto *mosq, void *userdata, int rc) {
    mqtt_publisher_t *publisher = (mqtt_publisher_t *)userdata;
    (void)mosq;
    if (publisher == NULL) {
        return;
    }
    atomic_store_explicit(&publisher->connected, 0, memory_order_relaxed);
    if (rc != 0) {
        fprintf(stderr, "[MQTT] disconnected unexpectedly: %s\n", mosquitto_strerror(rc));
    }
}

int mqtt_publisher_init(
    mqtt_publisher_t *publisher,
    const char *client_id,
    const char *host,
    int port,
    const char *topic_root
) {
    int rc = MOSQ_ERR_SUCCESS;

    if (publisher == NULL || client_id == NULL || host == NULL || topic_root == NULL) {
        return MQTT_PUBLISHER_ERR_INVALID;
    }

    memset(publisher, 0, sizeof(*publisher));
    atomic_init(&publisher->connected, 0);
    publisher->port = port;
    snprintf(publisher->client_id, sizeof(publisher->client_id), "%s", client_id);
    snprintf(publisher->host, sizeof(publisher->host), "%s", host);
    snprintf(publisher->topic_root, sizeof(publisher->topic_root), "%s", topic_root);

    rc = mosquitto_lib_init();
    if (rc != MOSQ_ERR_SUCCESS) {
        fprintf(stderr, "[MQTT] mosquitto_lib_init failed: %s\n", mosquitto_strerror(rc));
        return rc;
    }
    publisher->lib_initialized = 1;

    publisher->mosq = mosquitto_new(publisher->client_id, true, publisher);
    if (publisher->mosq == NULL) {
        fprintf(stderr, "[MQTT] mosquitto_new failed\n");
        mqtt_publisher_cleanup(publisher);
        return MOSQ_ERR_NOMEM;
    }

    mosquitto_connect_callback_set(publisher->mosq, on_connect);
    mosquitto_disconnect_callback_set(publisher->mosq, on_disconnect);
    mosquitto_reconnect_delay_set(publisher->mosq, 1u, 5u, true);

    rc = mosquitto_connect_async(publisher->mosq, publisher->host, publisher->port, 30);
    if (rc != MOSQ_ERR_SUCCESS) {
        fprintf(stderr, "[MQTT] connect_async failed: %s\n", mosquitto_strerror(rc));
        mqtt_publisher_cleanup(publisher);
        return rc;
    }

    rc = mosquitto_loop_start(publisher->mosq);
    if (rc != MOSQ_ERR_SUCCESS) {
        fprintf(stderr, "[MQTT] loop_start failed: %s\n", mosquitto_strerror(rc));
        mqtt_publisher_cleanup(publisher);
        return rc;
    }
    publisher->loop_started = 1;
    return 0;
}

void mqtt_publisher_cleanup(mqtt_publisher_t *publisher) {
    if (publisher == NULL) {
        return;
    }

    if (publisher->mosq != NULL) {
        if (publisher->loop_started) {
            (void)mosquitto_disconnect(publisher->mosq);
            (void)mosquitto_loop_stop(publisher->mosq, true);
        }
        mosquitto_destroy(publisher->mosq);
        publisher->mosq = NULL;
    }

    if (publisher->lib_initialized) {
        mosquitto_lib_cleanup();
        publisher->lib_initialized = 0;
    }

    atomic_store_explicit(&publisher->connected, 0, memory_order_relaxed);
    publisher->loop_started = 0;
}

int mqtt_publisher_publish(
    mqtt_publisher_t *publisher,
    const char *subtopic,
    const char *payload,
    int retain
) {
    char topic[192];
    int n = 0;

    if (publisher == NULL || publisher->mosq == NULL || subtopic == NULL || payload == NULL) {
        return MQTT_PUBLISHER_ERR_INVALID;
    }
    if (!atomic_load_explicit(&publisher->connected, memory_order_relaxed)) {
        return MQTT_PUBLISHER_ERR_NO_CONN;
    }

    n = snprintf(topic, sizeof(topic), "%s/%s", publisher->topic_root, subtopic);
    if (n < 0 || (size_t)n >= sizeof(topic)) {
        return MQTT_PUBLISHER_ERR_TOPIC_TOO_LONG;
    }

    return mosquitto_publish(
        publisher->mosq,
        NULL,
        topic,
        (int)strlen(payload),
        payload,
        0,
        retain != 0
    );
}

int mqtt_publisher_is_connected(const mqtt_publisher_t *publisher) {
    if (publisher == NULL) {
        return 0;
    }
    return atomic_load_explicit(&publisher->connected, memory_order_relaxed);
}

const char *mqtt_publisher_error_string(int rc) {
    switch (rc) {
        case 0:
            return "success";
        case MQTT_PUBLISHER_ERR_INVALID:
            return "invalid publisher or payload";
        case MQTT_PUBLISHER_ERR_NO_CONN:
            return "broker not connected";
        case MQTT_PUBLISHER_ERR_TOPIC_TOO_LONG:
            return "topic too long";
        default:
            return mosquitto_strerror(rc);
    }
}
