#include "rf_db.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#ifdef RF_HAS_SQLITE
#include <sqlite3.h>
#endif

int rf_db_init(rf_db_t *db, const char *db_path) {
    if (db == NULL) {
        return -1;
    }
    db->handle = NULL;
    db->enabled = 0;

#ifdef RF_HAS_SQLITE
    {
        sqlite3 *conn = NULL;
        const char *sql =
            "CREATE TABLE IF NOT EXISTS device_table ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "address TEXT NOT NULL,"
            "button TEXT NOT NULL,"
            "name TEXT,"
            "last_time TEXT NOT NULL,"
            "UNIQUE(address, button)"
            ");";
        char *err = NULL;
        if (sqlite3_open(db_path != NULL ? db_path : "./rf_device.db", &conn) != SQLITE_OK) {
            return -2;
        }
        if (sqlite3_exec(conn, sql, NULL, NULL, &err) != SQLITE_OK) {
            sqlite3_free(err);
            sqlite3_close(conn);
            return -3;
        }
        db->handle = conn;
        db->enabled = 1;
    }
#else
    (void)db_path;
    db->enabled = 0;
#endif

    return 0;
}

int rf_db_upsert(rf_db_t *db, const rf_decoded_packet_t *pkt, const char *name_hint) {
    if (db == NULL || pkt == NULL) {
        return -1;
    }

#ifdef RF_HAS_SQLITE
    {
        sqlite3 *conn = (sqlite3 *)db->handle;
        sqlite3_stmt *stmt = NULL;
        char ts[32];
        time_t now = time(NULL);
        struct tm tm_now;
        const char *name = (name_hint != NULL) ? name_hint : "unknown_device";
        const char *sql =
            "INSERT INTO device_table(address, button, name, last_time) "
            "VALUES(?1, ?2, ?3, ?4) "
            "ON CONFLICT(address, button) DO UPDATE SET "
            "name=excluded.name, last_time=excluded.last_time;";

        if (!db->enabled || conn == NULL) {
            return 0;
        }

#if defined(_WIN32)
        localtime_s(&tm_now, &now);
#else
        localtime_r(&now, &tm_now);
#endif
        strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tm_now);

        if (sqlite3_prepare_v2(conn, sql, -1, &stmt, NULL) != SQLITE_OK) {
            return -2;
        }
        sqlite3_bind_text(stmt, 1, pkt->addr, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, pkt->key, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, name, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, ts, -1, SQLITE_TRANSIENT);

        if (sqlite3_step(stmt) != SQLITE_DONE) {
            sqlite3_finalize(stmt);
            return -3;
        }
        sqlite3_finalize(stmt);
    }
#else
    (void)name_hint;
#endif

    return 0;
}

int rf_db_query_by_address(rf_db_t *db, const char *address, char *name_out, size_t name_cap) {
    if (name_out != NULL && name_cap > 0u) {
        name_out[0] = '\0';
    }
    if (db == NULL || address == NULL) {
        return -1;
    }

#ifdef RF_HAS_SQLITE
    {
        sqlite3 *conn = (sqlite3 *)db->handle;
        sqlite3_stmt *stmt = NULL;
        const char *sql =
            "SELECT name FROM device_table WHERE address=?1 ORDER BY last_time DESC LIMIT 1;";
        if (!db->enabled || conn == NULL || name_out == NULL || name_cap == 0u) {
            return 0;
        }
        if (sqlite3_prepare_v2(conn, sql, -1, &stmt, NULL) != SQLITE_OK) {
            return -2;
        }
        sqlite3_bind_text(stmt, 1, address, -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char *txt = sqlite3_column_text(stmt, 0);
            if (txt != NULL) {
                snprintf(name_out, name_cap, "%s", (const char *)txt);
                sqlite3_finalize(stmt);
                return 1;
            }
        }
        sqlite3_finalize(stmt);
    }
#else
    (void)address;
#endif

    return 0;
}

void rf_db_close(rf_db_t *db) {
    if (db == NULL) {
        return;
    }
#ifdef RF_HAS_SQLITE
    if (db->enabled && db->handle != NULL) {
        sqlite3_close((sqlite3 *)db->handle);
    }
#endif
    db->enabled = 0;
    db->handle = NULL;
}

