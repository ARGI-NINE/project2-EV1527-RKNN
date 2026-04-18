#ifndef RF_SOURCE_H
#define RF_SOURCE_H

#define RF_SOURCE_UART9_PATH "/dev/ttyS9"

int rf_source_is_supported_path(const char *path);
int rf_source_open(const char *path);
void rf_source_close(int fd);

#endif
