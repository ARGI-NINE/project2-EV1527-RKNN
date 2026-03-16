#include "rf_epoll.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#if defined(__linux__)
#include <sys/epoll.h>
#include <unistd.h>
#else
#include <fcntl.h>
#include <io.h>
#endif

static int consume_rf_stream(const rf_epoll_config_t *cfg, rf_proto_parser_t *parser) {
    unsigned char buf[256];
    int n = 0;
    int i = 0;
    rf_frame_t frame;

#if defined(__linux__)
    n = (int)read(cfg->rf_fd, buf, sizeof(buf));
#else
    n = _read(cfg->rf_fd, buf, (unsigned)sizeof(buf));
#endif
    if (n < 0) {
#if defined(__linux__)
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0;
        }
#endif
        return -1;
    }
    if (n == 0) {
        return -2;
    }

    for (i = 0; i < n; ++i) {
        const int rc = rf_proto_parser_consume(parser, (uint8_t)buf[i], &frame);
        if (rc == 1 && cfg->on_frame != NULL) {
            if (cfg->on_frame(&frame, cfg->user) != 0) {
                return 1;
            }
        }
    }
    return 0;
}

int rf_epoll_run(const rf_epoll_config_t *cfg) {
    rf_proto_parser_t parser;
    if (cfg == NULL || cfg->rf_fd < 0 || cfg->on_frame == NULL) {
        return -1;
    }
    rf_proto_parser_init(&parser);

#if defined(__linux__)
    {
        int epfd = -1;
        struct epoll_event ev;
        struct epoll_event events[8];
        int nfds = 0;
        int i = 0;
        int rc = 0;

        epfd = epoll_create1(0);
        if (epfd < 0) {
            return -2;
        }

        memset(&ev, 0, sizeof(ev));
        ev.events = EPOLLIN;
        ev.data.fd = cfg->rf_fd;
        if (epoll_ctl(epfd, EPOLL_CTL_ADD, cfg->rf_fd, &ev) != 0) {
            close(epfd);
            return -3;
        }
        if (cfg->mqtt_fd >= 0) {
            memset(&ev, 0, sizeof(ev));
            ev.events = EPOLLIN;
            ev.data.fd = cfg->mqtt_fd;
            (void)epoll_ctl(epfd, EPOLL_CTL_ADD, cfg->mqtt_fd, &ev);
        }

        while (1) {
            nfds = epoll_wait(epfd, events, (int)(sizeof(events) / sizeof(events[0])), -1);
            if (nfds < 0) {
                if (errno == EINTR) {
                    continue;
                }
                close(epfd);
                return -4;
            }
            for (i = 0; i < nfds; ++i) {
                if (events[i].data.fd == cfg->rf_fd) {
                    rc = consume_rf_stream(cfg, &parser);
                    if (rc != 0) {
                        close(epfd);
                        return rc;
                    }
                } else if (cfg->mqtt_fd >= 0 && events[i].data.fd == cfg->mqtt_fd) {
                    if (cfg->on_mqtt != NULL) {
                        rc = cfg->on_mqtt(cfg->mqtt_fd, cfg->user);
                        if (rc != 0) {
                            close(epfd);
                            return rc;
                        }
                    }
                }
            }
        }
    }
#else
    while (1) {
        const int rc = consume_rf_stream(cfg, &parser);
        if (rc != 0) {
            return rc;
        }
        if (cfg->mqtt_fd >= 0 && cfg->on_mqtt != NULL) {
            const int mrc = cfg->on_mqtt(cfg->mqtt_fd, cfg->user);
            if (mrc != 0) {
                return mrc;
            }
        }
    }
#endif
}

