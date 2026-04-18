#include "rf_source.h"

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#include <stdio.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

int rf_source_open(const char *path) {
    if (path == NULL) {
        path = "/dev/rf433";
    }
    if (path[0] == '-' && path[1] == '\0') {
#if defined(_WIN32)
        int fd = _fileno(stdin);
        if (fd < 0) {
            HANDLE h_stdin = GetStdHandle(STD_INPUT_HANDLE);
            if (h_stdin != NULL && h_stdin != INVALID_HANDLE_VALUE) {
                fd = _open_osfhandle((intptr_t)h_stdin, _O_RDONLY | _O_BINARY);
            }
        }
        if (fd >= 0) {
            (void)_setmode(fd, _O_BINARY);
        }
        return fd;
#else
        return 0;
#endif
    }

#if defined(_WIN32)
    return _open(path, _O_RDONLY | _O_BINARY);
#else
    return open(path, O_RDONLY | O_NONBLOCK);
#endif
}

void rf_source_close(int fd) {
    if (fd < 0) {
        return;
    }
#if defined(_WIN32)
    _close(fd);
#else
    close(fd);
#endif
}
