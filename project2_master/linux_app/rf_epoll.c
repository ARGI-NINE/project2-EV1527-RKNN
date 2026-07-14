#include "rf_epoll.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <unistd.h>

#include "rf433_ioctl.h"

static int create_stats_timer(int epfd, int interval_s) {
    struct itimerspec its;
    struct epoll_event ev;
    int timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);

    if (timer_fd < 0) {
        fprintf(stderr, "[RF_IO] stats timer unavailable: %s\n", strerror(errno));
        return -1;
    }

    memset(&its, 0, sizeof(its));
    its.it_value.tv_sec = interval_s;
    its.it_interval.tv_sec = interval_s;
    if (timerfd_settime(timer_fd, 0, &its, NULL) != 0) {
        fprintf(stderr, "[RF_IO] stats timer configuration failed: %s\n", strerror(errno));
        close(timer_fd);
        return -1;
    }

    memset(&ev, 0, sizeof(ev));
    ev.events = EPOLLIN;
    ev.data.fd = timer_fd;
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, timer_fd, &ev) != 0) {
        fprintf(stderr, "[RF_IO] stats timer epoll registration failed: %s\n", strerror(errno));
        close(timer_fd);
        return -1;
    }

    return timer_fd;
}

static int consume_frames(const rf_epoll_config_t *cfg) {
    struct rf433_frame drv_frame;
    rf_frame_t frame;
    ssize_t n;

    while (1) {
        n = read(cfg->rf_fd, &drv_frame, sizeof(drv_frame));
        if (n < 0) {
            if (errno == EINTR) {
                if (cfg->stats != NULL) {
                    cfg->stats->read_eintr++;
                }
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                if (cfg->stats != NULL) {
                    cfg->stats->read_eagain++;
                }
                return 0;
            }
            if (cfg->stats != NULL) {
                cfg->stats->read_error++;
            }
            fprintf(stderr, "[RF_IO] read failed: errno=%d (%s)\n", errno, strerror(errno));
            return -1;
        }
        if (n == 0) {
            if (cfg->stats != NULL) {
                cfg->stats->read_eof++;
            }
            fprintf(stderr, "[RF_IO] read returned EOF on rf_fd=%d\n", cfg->rf_fd);
            return -2;
        }
        if ((size_t)n < sizeof(drv_frame)) {
            if (cfg->stats != NULL) {
                cfg->stats->short_read++;
            }
            fprintf(stderr, "[RF_IO] short read from driver: %zd/%zu\n", n, sizeof(drv_frame));
            continue;
        }

        memset(&frame, 0, sizeof(frame));
        frame.len = drv_frame.pulse_count;
        if (frame.len > RF_BUFFER_SIZE) {
            frame.len = (uint16_t)RF_BUFFER_SIZE;
        }
        memcpy(frame.pulse, drv_frame.pulse, frame.len * sizeof(uint16_t));

        if (cfg->on_frame != NULL) {
            if (cfg->on_frame(&frame, drv_frame.timestamp_ns, drv_frame.seq, cfg->user) != 0) {
                return 1;
            }
        }
    }
}

int rf_epoll_run(const rf_epoll_config_t *cfg) {
    int epfd = -1;
    int timer_fd = -1;
    struct epoll_event ev;
    struct epoll_event events[8];
    int nfds = 0;
    int i = 0;
    int rc = 0;

    if (cfg == NULL || cfg->rf_fd < 0 || cfg->on_frame == NULL) {
        return -1;
    }

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

    if (cfg->on_stats != NULL && cfg->stats_interval_s > 0) {
        /* Statistics are optional; RF frame delivery remains available if setup fails. */
        timer_fd = create_stats_timer(epfd, cfg->stats_interval_s);
    }

    while (1) {
        nfds = epoll_wait(epfd, events, (int)(sizeof(events) / sizeof(events[0])), -1);
        if (nfds < 0) {
            if (errno == EINTR) {
                if (cfg->stats != NULL) {
                    cfg->stats->epoll_eintr++;
                }
                continue;
            }
            if (cfg->stats != NULL) {
                cfg->stats->epoll_error++;
            }
            fprintf(stderr, "[RF_IO] epoll_wait failed: errno=%d (%s)\n", errno, strerror(errno));
            rc = -4;
            goto out;
        }
        for (i = 0; i < nfds; ++i) {
            if (events[i].data.fd == cfg->rf_fd) {
                if ((events[i].events & (EPOLLERR | EPOLLHUP)) != 0u) {
                    fprintf(
                        stderr,
                        "[RF_IO] epoll event=0x%X on rf_fd=%d (ERR/HUP)\n",
                        (unsigned)events[i].events,
                        cfg->rf_fd
                    );
                }
                if ((events[i].events & (EPOLLIN | EPOLLERR | EPOLLHUP)) == 0u) {
                    continue;
                }
                rc = consume_frames(cfg);
                if (rc != 0) {
                    goto out;
                }
            } else if (timer_fd >= 0 && events[i].data.fd == timer_fd) {
                uint64_t expirations;
                ssize_t n = read(timer_fd, &expirations, sizeof(expirations));
                if (n != (ssize_t)sizeof(expirations)) {
                    if (n < 0 && (errno == EAGAIN || errno == EINTR)) {
                        continue;
                    }
                    fprintf(stderr, "[RF_IO] stats timer read failed: %s\n", n < 0 ? strerror(errno) : "short read");
                    continue;
                }
                if (cfg->on_stats != NULL) {
                    cfg->on_stats(cfg->rf_fd, cfg->user);
                }
            }
        }
    }

out:
    if (timer_fd >= 0) {
        close(timer_fd);
    }
    close(epfd);
    return rc;
}
