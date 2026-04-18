#ifndef RF_EPOLL_H
#define RF_EPOLL_H

#include "../common/rf_protocol.h"

typedef int (*rf_epoll_on_frame_fn)(const rf_frame_t *frame, void *user);

typedef struct {
    uint32_t read_eintr;
    uint32_t read_eagain;
    uint32_t read_eof;
    uint32_t read_error;
    uint32_t epoll_eintr;
    uint32_t epoll_error;
    uint32_t parse_error;
    uint32_t crc_error;
} rf_epoll_stats_t;

typedef struct {
    int rf_fd;
    rf_epoll_on_frame_fn on_frame;
    rf_epoll_stats_t *stats;
    void *user;
} rf_epoll_config_t;

int rf_epoll_run(const rf_epoll_config_t *cfg);

#endif
