#include <stdlib.h>
#include <stdint.h>
#include <sqlite3.h>
#include "../log.h"
#include "../db.h"

#pragma comment(lib, "sqlite10.lib")

#define DB_CHECK(x) if (!x) goto failed;

#define DB_NAME "ed2kd.db"

static sqlite3 *g_db;

int db_open()
{
	static const char query[] = 
		"CREATE TABLE IF NOT EXISTS files ("
		"	hash TEXT PRIMARY KEY,"
		"	name TEXT,"
		"	size INTEGER,"
		"	rating INTEGER,"
		"	last_seen INTEGER,"
		"	src_cnt INTEGER,"
		"	full_src_cnt INTEGER"
		");"

		"CREATE TABLE IF NOT EXISTS sources ("
		"	hash TEXT,"
		"	client INTEGER"
		");"

		"CREATE INDEX IF NOT EXISTS souces_hash_i"
		"	ON sources(hash);"

		"CREATE INDEX IF NOT EXISTS souces_client_i"
		"	ON sources(client);"

		"DELETE FROM sources;"
	;

	int err;
	int flags = SQLITE_OPEN_CREATE|SQLITE_OPEN_READWRITE|SQLITE_OPEN_FULLMUTEX|SQLITE_OPEN_SHAREDCACHE;
	err = sqlite3_open_v2(DB_NAME, &g_db, flags, NULL);
	if ( SQLITE_OK == err ) {
		char *errmsg;
		
		err = sqlite3_exec(g_db, query, NULL, NULL, &errmsg);
		if ( SQLITE_OK != err ) {
			ED2KD_LOGERR("failed to execute database init script (%s)", errmsg);
			sqlite3_free(errmsg);
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
	sqlite3_stmt *stmt;
	char *tail;
	static const char query[] = 
		"INSERT OR REPLACE INTO files(hash,name,size) VALUES(?,?,?)";

	DB_CHECK( SQLITE_OK == sqlite3_prepare_v2(g_db, query, sizeof query, &stmt, &tail) );
	DB_CHECK( SQLITE_OK == sqlite3_bind_text(stmt, 1, (const char*)file->hash, sizeof file->hash, SQLITE_STATIC) );
	DB_CHECK( SQLITE_OK == sqlite3_bind_text(stmt, 2, file->name, file->name_len, SQLITE_STATIC) );
	DB_CHECK( SQLITE_OK == sqlite3_bind_int64(stmt, 3, file->size) );
	DB_CHECK( SQLITE_DONE == sqlite3_step(stmt) );
	return 0;

failed:
	ED2KD_LOGERR("failed to add file to db (%s)", sqlite3_errmsg(g_db));
	return -1;
}

int db_remove_source( const struct e_client *owner )
{
	return 0;
}

int db_get_sources( const unsigned char *hash, struct e_source *sources, size_t *size )
{
	return 0;
}