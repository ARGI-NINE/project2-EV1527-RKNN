#ifndef RF_EPOLL_H
#define RF_EPOLL_H

#include "../common/rf_protocol.h"

typedef int (*rf_epoll_on_frame_fn)(const rf_frame_t *frame, void *user);

typedef struct {
    int rf_fd;
    int rewind_on_eof;
    rf_epoll_on_frame_fn on_frame;
    void *user;
} rf_epoll_config_t;

int rf_epoll_run(const rf_epoll_config_t *cfg);

#endif

