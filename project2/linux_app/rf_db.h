#ifndef RF_DB_H
#define RF_DB_H

#include <stddef.h>

#include "rf_decode.h"

typedef struct {
    void *handle;
    int enabled;
} rf_db_t;

int rf_db_init(rf_db_t *db, const char *db_path);
int rf_db_upsert(rf_db_t *db, const rf_decoded_packet_t *pkt, const char *name_hint);
int rf_db_query_by_address(rf_db_t *db, const char *address, char *name_out, size_t name_cap);
void rf_db_close(rf_db_t *db);

#endif

