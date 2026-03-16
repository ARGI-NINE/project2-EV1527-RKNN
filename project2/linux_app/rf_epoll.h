#ifndef RF_EPOLL_H
#define RF_EPOLL_H

#include "../common/rf_protocol.h"

typedef int (*rf_epoll_on_frame_fn)(const rf_frame_t *frame, void *user);
typedef int (*rf_epoll_on_mqtt_fn)(int mqtt_fd, void *user);

typedef struct {
    int rf_fd;
    int mqtt_fd;
    rf_epoll_on_frame_fn on_frame;
    rf_epoll_on_mqtt_fn on_mqtt;
    void *user;
} rf_epoll_config_t;

int rf_epoll_run(const rf_epoll_config_t *cfg);

#endif

