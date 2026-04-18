#include "rf_source.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

/**
 * Configure RK3568 UART9 to match STM32 side:
 *   9600 baud, 8 data bits, no parity, 1 stop bit, no flow control.
 *
 * Reference: iTOP-RK3568 serial programming guidance.
 */
static int rf_source_configure_serial(int fd) {
    struct termios tty;

    if (tcgetattr(fd, &tty) != 0) {
        return -1;
    }

    /* Raw mode: disable all input/output processing. */
    cfmakeraw(&tty);

    /* Baud rate: 9600 (must match STM32 RF_Uart.c RF_UART_BAUDRATE). */
    cfsetispeed(&tty, B9600);
    cfsetospeed(&tty, B9600);

    /* 8 data bits */
    tty.c_cflag = (tty.c_cflag & (tcflag_t)~CSIZE) | CS8;

    /* No parity */
    tty.c_cflag &= (tcflag_t)~(PARENB | PARODD);
    tty.c_iflag &= (tcflag_t)~INPCK;

    /* 1 stop bit */
    tty.c_cflag &= (tcflag_t)~CSTOPB;

    /* No hardware flow control */
    tty.c_cflag &= (tcflag_t)~CRTSCTS;

    /* Enable receiver, ignore modem control lines */
    tty.c_cflag |= (CLOCAL | CREAD);

    /* Non-blocking tty settings (open() also uses O_NONBLOCK). */
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 0;

    /* Flush then apply */
    tcflush(fd, TCIOFLUSH);

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        return -1;
    }

    return 0;
}

/**
 * Return non-zero when @p path matches the single supported board-side path.
 */
int rf_source_is_supported_path(const char *path) {
    if (path == NULL || path[0] == '\0') {
        return 1;
    }
    return (strcmp(path, RF_SOURCE_UART9_PATH) == 0) ? 1 : 0;
}

static int rf_source_is_char_device(const char *path) {
    struct stat st;

    if (path == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (stat(path, &st) != 0) {
        return -1;
    }
    return S_ISCHR(st.st_mode) ? 1 : 0;
}

int rf_source_open(const char *path) {
    int fd = -1;
    const char *resolved_path = path;

    if (resolved_path == NULL || resolved_path[0] == '\0') {
        resolved_path = RF_SOURCE_UART9_PATH;
    }

    if (!rf_source_is_supported_path(resolved_path)) {
        errno = EINVAL;
        return -1;
    }

    {
        const int path_type = rf_source_is_char_device(resolved_path);
        if (path_type < 0) {
            return -1;
        }
        if (path_type == 0) {
            errno = ENOTTY;
            return -1;
        }
    }

    fd = open(resolved_path, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        return -1;
    }

    if (rf_source_configure_serial(fd) != 0) {
        close(fd);
        return -1;
    }

    return fd;
}

void rf_source_close(int fd) {
    if (fd < 0) {
        return;
    }
    close(fd);
}
