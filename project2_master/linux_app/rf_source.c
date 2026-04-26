#include "rf_source.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <string.h>
#include <unistd.h>

int rf_source_is_supported_path(const char *path) {
    if (path == NULL || path[0] == '\0') {
        return 1;
    }
    return (strcmp(path, RF_SOURCE_PATH) == 0) ? 1 : 0;
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
    const char *resolved = (path == NULL || path[0] == '\0') ? RF_SOURCE_PATH : path;

    if (!rf_source_is_supported_path(resolved)) {
        errno = EINVAL;
        return -1;
    }

    {
        const int is_chr = rf_source_is_char_device(resolved);
        if (is_chr < 0) {
            return -1;
        }
        if (is_chr == 0) {
            errno = ENOTTY;
            return -1;
        }
    }

    return open(resolved, O_RDONLY | O_NONBLOCK);
}

void rf_source_close(int fd) {
    if (fd < 0) {
        return;
    }
    close(fd);
}
