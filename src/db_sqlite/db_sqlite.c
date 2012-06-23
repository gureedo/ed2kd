#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <malloc.h>
#include <sqlite3.h>
#include "../log.h"
#include "../db.h"
#include "../client.h"

#pragma comment(lib, "sqlite10.lib")

static uint64_t
sdbm( const unsigned char *str, size_t length )
{
    uint64_t hash = 0;
    size_t i;

    for( i=0; i<length; ++i )
        hash = (*str++) + (hash << 6) + (hash << 16) - hash;

    return hash;
}

#define DB_NAME "ed2kd.db"
#define MAX_SEARCH_QUERY_LEN  1024
#define MAX_NAME_TERM_LEN     1024

#define DB_CHECK(x) if (!(x)) goto failed;
#define MAKE_FID(x) sdbm((x), 16)
#define MAKE_SID(x) ( ((uint64_t)(x)->ip<<32) & (uint64_t)(x)->port )

static sqlite3 *g_db;

int db_open()
{
	static const char query[] = 
		"CREATE TABLE IF NOT EXISTS files ("
        "   fid INTEGER PRIMARY KEY,"
		"	hash BLOB NOT NULL,"
		"	name TEXT NOT NULL,"
        "   ext TEXT,"
		"	size INTEGER NOT NULL,"
        "   type INTEGER NOT NULL,"
        "   mlength INTEGER,"
        "   mbitrate INTEGER,"
        "   mcodec TEXT,"
		"	last_seen INTEGER NOT NULL"
		");"

		"CREATE TABLE IF NOT EXISTS sources ("
		"	fid INTEGER NOT NULL,"
		"	sid INTEGER NOT NULL,"
        "   complete INTEGER"
		");"
		"CREATE INDEX IF NOT EXISTS souces_hash_i"
		"	ON sources(fid);"
		"CREATE INDEX IF NOT EXISTS souces_client_i"
		"	ON sources(sid);"

		"DELETE FROM sources;"

        "CREATE VIRTUAL TABLE IF NOT EXISTS fnames USING fts4 ("
        "   content=\"files\", tokenize=unicode61, name"
        ");"

        "CREATE TRIGGER IF NOT EXISTS files_bu BEFORE UPDATE ON files BEGIN"
        "   DELETE FROM fnames WHERE docid=old.rowid;"
        "END;"
        "CREATE TRIGGER IF NOT EXISTS files_bd BEFORE DELETE ON files BEGIN"
        "   DELETE FROM fnames WHERE docid=old.rowid;"
        "END;"

        "CREATE TRIGGER IF NOT EXISTS files_au AFTER UPDATE ON files BEGIN"
        "   INSERT INTO fnames(docid, name) VALUES(new.rowid, new.name);"
        "   new.last_seen = strftime('%s', 'now');"
        "END;"
        "CREATE TRIGGER IF NOT EXISTS files_ai AFTER INSERT ON files BEGIN"
        "   INSERT INTO fnames(docid, name) VALUES(new.rowid, new.name);"
        "   new.last_seen = strftime('%s', 'now');"
        "END;"
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
    static const char query1[] = 
        "INSERT OR REPLACE INTO files(fid,hash,name,ext,size,type,mlength,mbitrate,mcodec) "
        "   VALUES(?,?,?,?,?,?,?,?,?)";
    static const char query2[] = 
        "INSERT INTO sources(fid,sid,complete) VALUES(?,?,?)";

    sqlite3_stmt *stmt;
	char *tail;
    const char *ext;
    int ext_len;
    int i;
    uint64_t fid = MAKE_FID(file->hash);

    // find extension
    ext = file->name + file->name_len-1;
    while ( (file->name >= ext) && ('.' != *ext) ) {
        --ext;
        ++ext_len;
    };

    i=1;
    DB_CHECK( SQLITE_OK == sqlite3_prepare_v2(g_db, query1, sizeof query1, &stmt, &tail) );
	DB_CHECK( SQLITE_OK == sqlite3_bind_int64(stmt, i++,  fid) );
    DB_CHECK( SQLITE_OK == sqlite3_bind_blob(stmt, i++, file->hash, sizeof file->hash, SQLITE_STATIC) );
	DB_CHECK( SQLITE_OK == sqlite3_bind_text(stmt, i++, file->name, file->name_len, SQLITE_STATIC) );
    DB_CHECK( SQLITE_OK == sqlite3_bind_text(stmt, i++, ext, ext_len, SQLITE_STATIC) );
	DB_CHECK( SQLITE_OK == sqlite3_bind_int64(stmt, i++, file->size) );
    DB_CHECK( SQLITE_OK == sqlite3_bind_int(stmt, i++, file->type) );
    DB_CHECK( SQLITE_OK == sqlite3_bind_int(stmt, i++, file->media_length) );
    DB_CHECK( SQLITE_OK == sqlite3_bind_int(stmt, i++, file->media_bitrate) );
    DB_CHECK( SQLITE_OK == sqlite3_bind_text(stmt, i++, file->media_codec, file->media_codec_len, SQLITE_STATIC) );
	DB_CHECK( SQLITE_DONE == sqlite3_step(stmt) );
	sqlite3_finalize(stmt);

    i=1;
    DB_CHECK( SQLITE_OK == sqlite3_prepare_v2(g_db, query2, sizeof query2, &stmt, &tail) );
	DB_CHECK( SQLITE_OK == sqlite3_bind_int64(stmt, i++, fid) );
	DB_CHECK( SQLITE_OK == sqlite3_bind_int64(stmt, i++, MAKE_SID(owner)) );
    DB_CHECK( SQLITE_OK == sqlite3_bind_int(stmt, i++, file->complete) );
	DB_CHECK( SQLITE_DONE == sqlite3_step(stmt) );
	sqlite3_finalize(stmt);

	return 0;

failed:
	if ( stmt ) sqlite3_finalize(stmt);
	ED2KD_LOGERR("failed to add file to db (%s)", sqlite3_errmsg(g_db));
	return -1;
}

int db_remove_source( const struct e_client *owner )
{
	sqlite3_stmt *stmt;
	char *tail;
	static const char query[] = 
		"DELETE FROM sources WHERE sid=?";

	DB_CHECK( SQLITE_OK == sqlite3_prepare_v2(g_db, query, sizeof query, &stmt, &tail) );
	DB_CHECK( SQLITE_OK == sqlite3_bind_int64(stmt, 1, MAKE_SID(owner)) );
	DB_CHECK( SQLITE_DONE == sqlite3_step(stmt) );
	sqlite3_finalize(stmt);
	return 0;

failed:
	if ( stmt ) sqlite3_finalize(stmt);
	ED2KD_LOGERR("failed to remove sources from db (%s)", sqlite3_errmsg(g_db));
	return -1;
}

int db_search_file( struct search_node *snode, struct e_file *files, size_t *count )
{
    sqlite3_stmt *stmt;
    char *tail;
    char query[MAX_SEARCH_QUERY_LEN] = {0};
    char name_term[MAX_NAME_TERM_LEN] = {0};
    size_t i;
    int err;
    uint64_t minsize=0, maxsize=0, srcavail=0, srccomplete=0, minbitrate=0, minlength=0;
    struct search_node *ext_node, *codec_node;

    strcat(query, "SELECT f.hash,f.name,f.size FROM fnames n JOIN files f ON f.id = n.docid WHERE fnames MATCH ?");

    while ( snode ) {
        if ( (ST_AND <= snode->type) && (ST_NOT >= snode->type) ) {
            if ( !snode->left_visited ) {
                if ( snode->string_term ) {
                    strcat(name_term, "(");
                }
                snode->left_visited = 1;
                snode = snode->left;
                continue;
            } else if ( !snode->right_visited ) {
                if ( snode->string_term ) {
                    const char *oper = 0;
                    switch( snode->type ) {
                    case ST_AND:
                        oper = " AND ";
                        break;
                    case ST_OR:
                        oper = " OR ";
                        break;
                    case ST_NOT:
                        oper = " NOT ";
                        break;

                    default:
                        DB_CHECK(0);
                    }
                    strcat(name_term, oper);
                }
                snode->right_visited = 1;
                snode = snode->right;
                continue;
            } else {
                if ( snode->string_term ) {
                    strcat(name_term, ")");
                }
            }
        } else {
            

            switch ( snode->type ) {
            case ST_STRING:
                DB_CHECK(!snode->parent || snode->parent->string_term);
                strncat(name_term, snode->str_val, snode->str_len);
                break;
            case ST_EXTENSION:
                ext_node = snode;
                break;
            case ST_CODEC:
                codec_node = snode;
                break;
            case ST_MINSIZE:
                minsize = snode->int_val;
                break;
            case ST_MAXSIZE:
                maxsize = snode->int_val;
                break;
            case ST_SRCAVAIL:
                srcavail = snode->int_val;
                break;
            case ST_SRCCOMLETE:
                srccomplete = snode->int_val;
                break;
            case ST_MINBITRATE:
                minbitrate = snode->int_val;
                break;
            case ST_MINLENGTH:
                minlength = snode->int_val;
                break;
            default:
                DB_CHECK(0);
            }
        }

        snode = snode->parent;
    }

    if ( ext_node ) {
        //strcat(query, " AND ext=?");
    }
    if ( codec_node ) {
        //strcat(query, " AND codec=?");
    }
    if ( minsize ) {
        strcat(query, " AND size>?");
    }
    if ( maxsize ) {
        strcat(query, " AND size<?");
    }
    if ( srcavail ) {
        //strcat(query, " AND srcavail>?");
    }
    if ( srccomplete ) {
        //strcat(query, " AND srccomplete>?");
    }
    if ( minbitrate ) {
        //strcat(query, " AND bitrate>?");
    }
    if ( minlength ) {
        //strcat(query, " AND length>?");
    }

    DB_CHECK( SQLITE_OK == sqlite3_prepare_v2(g_db, query, strlen(query)+1, &stmt, &tail) );

    i=1;
    DB_CHECK( SQLITE_OK == sqlite3_bind_text(stmt, i++, name_term, strlen(name_term)+1, SQLITE_STATIC) );
    
    if ( ext_node ) {
        //DB_CHECK( SQLITE_OK == sqlite3_bind_text(stmt, i++, ext_node->str_val, ext_node->str_len, SQLITE_STATIC) );
    }
    if ( codec_node ) {
        //DB_CHECK( SQLITE_OK == sqlite3_bind_text(stmt, i++, codec_node->str_val, codec_node->str_len, SQLITE_STATIC) );
    }
    if ( minsize ) {
        DB_CHECK( SQLITE_OK == sqlite3_bind_int64(stmt, i++, minsize) );
    }
    if ( maxsize ) {
        DB_CHECK( SQLITE_OK == sqlite3_bind_int64(stmt, i++, maxsize) );
    }
    if ( srcavail ) {
        //DB_CHECK( SQLITE_OK == sqlite3_bind_int64(stmt, i++, srcavail) );
    }
    if ( srccomplete ) {
        //DB_CHECK( SQLITE_OK == sqlite3_bind_int64(stmt, i++, srccomplete) );
    }
    if ( minbitrate ) {
        //DB_CHECK( SQLITE_OK == sqlite3_bind_int64(stmt, i++, minbitrate) );
    }
    if ( minlength ) {
        //DB_CHECK( SQLITE_OK == sqlite3_bind_int64(stmt, i++, minlength) );
    }



    memset(files, 0, sizeof(struct e_file)*(*count));
    i = 0;
    while ( ((err = sqlite3_step(stmt)) == SQLITE_ROW) && (i < *count) ) {
        int col = 0;
        
        memcpy(files[i].hash, sqlite3_column_blob(stmt, col++), sizeof(files[i].hash));
        
        files[i].name_len = sqlite3_column_bytes(stmt, col);
        strncpy(files[i].name, (const char*)sqlite3_column_text(stmt, col++), 
            files[i].name_len > MAX_FILENAME_LEN ? MAX_FILENAME_LEN : files[i].name_len);

        files[i].size = sqlite3_column_int64(stmt, col++);
        //files[i].srcavail = sqlite3_column_int64(stmt, col++);
        //files[i].srccomplete = sqlite3_column_int64(stmt, col++);
        // rating
        // type
        //files[i].m_length = sqlite3_column_int64(stmt, col++);
        //files[i].m_bitrate = sqlite3_column_int64(stmt, col++);
        
        // codec len
        //files[i].m_length = sqlite3_column_int64(stmt, col++);

        ++i;
    }

    DB_CHECK( (i==*count) || (SQLITE_DONE == err) );

    *count = i;
    return 0;

failed:
	if ( stmt ) sqlite3_finalize(stmt);
	ED2KD_LOGERR("failed perform search query (%s)", sqlite3_errmsg(g_db));

    return -1;
}

int db_get_sources( const unsigned char *hash, struct e_source *sources, size_t *count )
{
	sqlite3_stmt *stmt;
	char *tail;
    size_t i;
    int err;
	static const char query[] = 
		"SELECT source_id FROM sources WHERE hash_id=? LIMIT ?";

	DB_CHECK( SQLITE_OK == sqlite3_prepare_v2(g_db, query, sizeof query, &stmt, &tail) );
    DB_CHECK( SQLITE_OK == sqlite3_bind_int64(stmt, 1, sdbm(hash, 16)) );
    DB_CHECK( SQLITE_OK == sqlite3_bind_int(stmt, 2, *count) );

    i=0;
    while ( ((err = sqlite3_step(stmt)) == SQLITE_ROW) && (i < *count) ) {

        sources
        
        files[i].size = sqlite3_column_int64(stmt, 0);
        //files[i].srcavail = sqlite3_column_int64(stmt, col++);
        //files[i].srccomplete = sqlite3_column_int64(stmt, col++);
        // rating
        // type
        //files[i].m_length = sqlite3_column_int64(stmt, col++);
        //files[i].m_bitrate = sqlite3_column_int64(stmt, col++);

        // codec len
        //files[i].m_length = sqlite3_column_int64(stmt, col++);

        ++i;
    }

    DB_CHECK( (i==*count) || (SQLITE_DONE == err) );

    *count = i;


	return 0;

failed:
	if ( stmt ) sqlite3_finalize(stmt);
	ED2KD_LOGERR("failed to remove souves from db (%s)", sqlite3_errmsg(g_db));
	return -1;
}