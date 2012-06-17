#include <stdlib.h>
#include <stdint.h>
#include <sqlite3.h>
#include "../log.h"
#include "../db.h"

#pragma comment(lib, "sqlite10.lib")

#define DB_NAME "ed2kd.db"

static const char g_init_query[] = 
	"CREATE TABLE IF NOT EXISTS ed2kd.files ("
	"	hash TEXT PRIMARY KEY,"
	"	name TEXT,"
	"	size INTEGER,"
	"	rating INTEGER,"
	"	last_seen INTEGER,"
	"	src_cnt INTEGER,"
	"	full_src_cnt INTEGER"
	");"

	"CREATE TABLE IF NOT EXISTS ed2kd.sources ("
	"	hash TEXT,"
	"	client INTEGER"
	");"

	"CREATE INDEX IF NOT EXISTS ed2kd.souces_hash_i"
	"	ON sources(hash);"

	"CREATE INDEX IF NOT EXISTS ed2kd.souces_client_i"
	"	ON sources(client);"
;

static sqlite3 *g_db;

int db_open()
{
	int err;
	int flags = SQLITE_OPEN_CREATE|SQLITE_OPEN_READWRITE|SQLITE_OPEN_FULLMUTEX|SQLITE_OPEN_SHAREDCACHE;
	err = sqlite3_open_v2(DB_NAME, &g_db, flags, NULL);
	if ( SQLITE_OK == err ) {
		sqlite3_stmt *stmt;
		char *ptr;

		err = sqlite3_prepare_v2(g_db, g_init_query, sizeof g_init_query, &stmt, &ptr);
		if ( SQLITE_OK == err ) {
			err = sqlite3_step(stmt);
			if ( SQLITE_DONE == err ) {

			} else {
				ED2KD_LOGERR("failed to execute init script (%s)", sqlite3_errmsg(g_db));
				return -1;
			}

			sqlite3_finalize(stmt);
		} else {
			ED2KD_LOGERR("failed to prepare init script (%s)", sqlite3_errmsg(g_db));
			return -1;
		}
	} else {
		ED2KD_LOGERR("failed to open/create DB (%s)", sqlite3_errmsg(g_db));
		return -1;
	}

	return 0;
}

int db_close()
{
	return (SQLITE_OK == sqlite3_close(g_db)) ? 0 : -1;
}

int db_add_file( const struct e_file *file, const struct e_client *owner )
{
	return 0;
}

int db_remove_source( const struct e_client *owner )
{
	return 0;
}

int db_get_sources( const unsigned char *hash, struct e_source *sources, size_t *size )
{
	return 0;
}