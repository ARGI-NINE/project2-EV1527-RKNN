#include "rf_epoll.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/epoll.h>
#include <unistd.h>

static const char *rf_parse_error_name(rf_proto_parser_error_t err) {
    switch (err) {
        case RF_PROTO_PARSER_ERR_INVALID_LEN:
            return "invalid_len";
        case RF_PROTO_PARSER_ERR_CRC_MISMATCH:
            return "crc_mismatch";
        case RF_PROTO_PARSER_ERR_INTERNAL:
            return "internal";
        case RF_PROTO_PARSER_ERR_NONE:
        default:
            return "unknown";
    }
}

static void rf_emit_parse_stats(const rf_epoll_config_t *cfg, rf_proto_parser_error_t err) {
    if (cfg == NULL || cfg->stats == NULL) {
        return;
    }
    printf(
        "[RF_PARSE_STATS] crc_errors=%u parse_errors=%u last_error=%s\n",
        (unsigned)cfg->stats->crc_error,
        (unsigned)cfg->stats->parse_error,
        rf_parse_error_name(err)
    );
}

static int consume_rf_stream(const rf_epoll_config_t *cfg, rf_proto_parser_t *parser) {
    unsigned char buf[256];
    ssize_t n = 0;
    int i = 0;
    rf_frame_t frame;

    while (1) {
        n = read(cfg->rf_fd, buf, sizeof(buf));
        if (n < 0) {
            if (errno == EINTR) {
                if (cfg->stats != NULL) {
                    cfg->stats->read_eintr++;
                }
                fprintf(stderr, "[RF_IO] read interrupted by signal, retrying\n");
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

        for (i = 0; i < n; ++i) {
            const int rc = rf_proto_parser_consume(parser, (uint8_t)buf[i], &frame);
            if (rc < 0) {
                const rf_proto_parser_error_t parse_err = parser->last_error;
                if (cfg->stats != NULL) {
                    cfg->stats->parse_error++;
                    if (parse_err == RF_PROTO_PARSER_ERR_CRC_MISMATCH) {
                        cfg->stats->crc_error++;
                    }
                }
                rf_emit_parse_stats(cfg, parse_err);
                continue;
            }
            if (rc == 1 && cfg->on_frame != NULL) {
                if (cfg->on_frame(&frame, cfg->user) != 0) {
                    return 1;
                }
            }
        }
    }

    return 0;
}

int rf_epoll_run(const rf_epoll_config_t *cfg) {
    rf_proto_parser_t parser;
    int epfd = -1;
    struct epoll_event ev;
    struct epoll_event events[8];
    int nfds = 0;
    int i = 0;
    int rc = 0;

    if (cfg == NULL || cfg->rf_fd < 0 || cfg->on_frame == NULL) {
        return -1;
    }
    rf_proto_parser_init(&parser);

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

    while (1) {
        nfds = epoll_wait(epfd, events, (int)(sizeof(events) / sizeof(events[0])), -1);
        if (nfds < 0) {
            if (errno == EINTR) {
                if (cfg->stats != NULL) {
                    cfg->stats->epoll_eintr++;
                }
                fprintf(stderr, "[RF_IO] epoll_wait interrupted by signal, continuing\n");
                continue;
            }
            if (cfg->stats != NULL) {
                cfg->stats->epoll_error++;
            }
            fprintf(stderr, "[RF_IO] epoll_wait failed: errno=%d (%s)\n", errno, strerror(errno));
            close(epfd);
            return -4;
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
                rc = consume_rf_stream(cfg, &parser);
                if (rc != 0) {
                    close(epfd);
                    return rc;
                }
            }
        }
    }
}
