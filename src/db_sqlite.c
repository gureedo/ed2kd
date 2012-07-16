#include "db.h"
#include <string.h>
#include <malloc.h>
#include <sqlite3.h>
#include "log.h"
#include "client.h"
#include "util.h"

#ifdef _MSC_VER
#pragma comment(lib, "sqlite10.lib")
#endif

static uint64_t
sdbm( const unsigned char *str, size_t length )
{
    uint64_t hash = 0;
    size_t i;

    for( i=0; i<length; ++i )
        hash = (*str++) + (hash << 6) + (hash << 16) - hash;

    return hash;
}

#define DB_NAME ":memory:"
#define MAX_SEARCH_QUERY_LEN  1024
#define MAX_NAME_TERM_LEN     1024

#define DB_CHECK(x)         if (!(x)) goto failed;
#define MAKE_FID(x)         sdbm((x), 16)
#define MAKE_SID(x)         ( ((uint64_t)(x)->id<<32) | (uint64_t)(x)->port )
#define GET_SID_ID(sid)     (uint32_t)((sid)>>32)
#define GET_SID_PORT(sid)   (uint16_t)(sid)

static sqlite3 *g_db;

int db_open()
{
    static const char query[] =
        "CREATE TABLE IF NOT EXISTS files ("
        "   fid INTEGER PRIMARY KEY,"
        "	hash BLOB NOT NULL,"
        "	name TEXT NOT NULL,"
        "   ext TEXT,"
        "   size INTEGER NOT NULL,"
        "   type INTEGER NOT NULL,"
        "   srcavail INTEGER DEFAULT 0,"
        "   srccomplete INTEGER DEFAULT 0,"
        "   rating INTEGER DEFAULT 0,"
        "   rated_count INTEGER DEFAULT 0,"
        "   mlength INTEGER,"
        "   mbitrate INTEGER,"
        "   mcodec TEXT"
        ");"

        "CREATE VIRTUAL TABLE IF NOT EXISTS fnames USING fts4 ("
        "   content=\"files\", tokenize=unicode61, name"
        ");"

        "CREATE TABLE IF NOT EXISTS sources ("
        "   fid INTEGER NOT NULL,"
        "   sid INTEGER NOT NULL,"
        "   complete INTEGER,"
        "   rating INTEGER"
        ");"
        "CREATE INDEX IF NOT EXISTS sources_fid_i"
        "   ON sources(fid);"
        "CREATE INDEX IF NOT EXISTS sources_sid_i"
        "   ON sources(sid);"

        "CREATE TRIGGER IF NOT EXISTS sources_ai AFTER INSERT ON sources BEGIN"
        "   UPDATE files SET srcavail=srcavail+1,srccomplete=srccomplete+new.complete,"
        "       rating=rating+new.rating, rated_count = CASE WHEN new.rating<>0 THEN rated_count+1 ELSE 0 END"
        "   WHERE fid=new.fid;"
        "END;"
        "CREATE TRIGGER IF NOT EXISTS sources_bd BEFORE DELETE ON sources BEGIN"
        "   UPDATE files SET srcavail=srcavail-1,srccomplete=srccomplete-old.complete,"
        "       rating=rating-old.rating, rated_count = CASE WHEN old.rating<>0 THEN rated_count-1 ELSE rated_count END"
        "   WHERE fid=old.fid;"
        "END;"

        // delete when no sources available
        " CREATE TRIGGER IF NOT EXISTS files_au AFTER UPDATE ON files WHEN new.srcavail=0 BEGIN"
        "   DELETE FROM files WHERE fid=new.fid;"
        "END;"

        // update on file name change
        "CREATE TRIGGER IF NOT EXISTS files_fts1 BEFORE UPDATE ON files WHEN new.name<>old.name BEGIN"
        "   DELETE FROM fnames WHERE docid=old.rowid;"
        "END;"
        "CREATE TRIGGER IF NOT EXISTS files_fts2 AFTER UPDATE ON files WHEN new.name<>old.name BEGIN"
        "   INSERT INTO fnames(docid, name) VALUES(new.rowid, new.name);"
        "END;"
        // delete
        "CREATE TRIGGER IF NOT EXISTS files_fts3 BEFORE DELETE ON files BEGIN"
        "   DELETE FROM fnames WHERE docid=old.rowid;"
        "END;"
        // insert
        "CREATE TRIGGER IF NOT EXISTS files_fts4 AFTER INSERT ON files BEGIN"
        "   INSERT INTO fnames(docid, name) VALUES(new.rowid, new.name);"
        "END;"

        "DELETE FROM files;"
        "DELETE FROM fnames;"
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

int db_add_file( const struct pub_file *file, const client_t *owner )
{
    static const char query1[] =
        "UPDATE files SET name=?,ext=?,size=?,type=?,mlength=?,mbitrate=?,mcodec=? WHERE fid=?";
    static const char query2[] =
        "INSERT OR REPLACE INTO files(fid,hash,name,ext,size,type,mlength,mbitrate,mcodec) "
        "   VALUES(?,?,?,?,?,?,?,?,?)";
    static const char query3[] =
        "INSERT INTO sources(fid,sid,complete,rating) VALUES(?,?,?,?)";

    sqlite3_stmt *stmt;
    const char *tail;
    const char *ext;
    int ext_len;
    int i;
    uint64_t fid = MAKE_FID(file->hash);

    // find extension
    ext = file->name + file->name_len-1;
    ext_len = 0;
    while ( (file->name <= ext) && ('.' != *ext) ) {
        ext--;
        ext_len++;
    };
    if ( file->name == ext ) {
        ext_len = 0;
    } else {
        ext++;
    }

    i=1;
    DB_CHECK( SQLITE_OK == sqlite3_prepare_v2(g_db, query1, sizeof query1, &stmt, &tail) );
    DB_CHECK( SQLITE_OK == sqlite3_bind_text(stmt, i++, file->name, file->name_len, SQLITE_STATIC) );
    DB_CHECK( SQLITE_OK == sqlite3_bind_text(stmt, i++, ext, ext_len, SQLITE_STATIC) );
    DB_CHECK( SQLITE_OK == sqlite3_bind_int64(stmt, i++, file->size) );
    DB_CHECK( SQLITE_OK == sqlite3_bind_int(stmt, i++, file->type) );
    DB_CHECK( SQLITE_OK == sqlite3_bind_int(stmt, i++, file->media_length) );
    DB_CHECK( SQLITE_OK == sqlite3_bind_int(stmt, i++, file->media_bitrate) );
    DB_CHECK( SQLITE_OK == sqlite3_bind_text(stmt, i++, file->media_codec, file->media_codec_len, SQLITE_STATIC) );
    DB_CHECK( SQLITE_OK == sqlite3_bind_int64(stmt, i++,  fid) );
    DB_CHECK( SQLITE_DONE == sqlite3_step(stmt) );
    sqlite3_finalize(stmt);

    if ( !sqlite3_changes(g_db) ) {
        i=1;
        DB_CHECK( SQLITE_OK == sqlite3_prepare_v2(g_db, query2, sizeof query2, &stmt, &tail) );
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
    }

    i=1;
    DB_CHECK( SQLITE_OK == sqlite3_prepare_v2(g_db, query3, sizeof query3, &stmt, &tail) );
    DB_CHECK( SQLITE_OK == sqlite3_bind_int64(stmt, i++, fid) );
    DB_CHECK( SQLITE_OK == sqlite3_bind_int64(stmt, i++, MAKE_SID(owner)) );
    DB_CHECK( SQLITE_OK == sqlite3_bind_int(stmt, i++, file->complete) );
    DB_CHECK( SQLITE_OK == sqlite3_bind_int(stmt, i++, file->rating) );
    DB_CHECK( SQLITE_DONE == sqlite3_step(stmt) );
    sqlite3_finalize(stmt);

    return 0;

failed:
    if ( stmt ) sqlite3_finalize(stmt);
    ED2KD_LOGERR("failed to add file to db (%s)", sqlite3_errmsg(g_db));
    return -1;
}

int db_remove_source( const client_t *client )
{
    sqlite3_stmt *stmt;
    const char *tail;
    static const char query[] =
        "DELETE FROM sources WHERE sid=?";

    DB_CHECK( SQLITE_OK == sqlite3_prepare_v2(g_db, query, sizeof query, &stmt, &tail) );
    DB_CHECK( SQLITE_OK == sqlite3_bind_int64(stmt, 1, MAKE_SID(client)) );
    DB_CHECK( SQLITE_DONE == sqlite3_step(stmt) );
    sqlite3_finalize(stmt);
    return 0;

failed:
    if ( stmt ) sqlite3_finalize(stmt);
    ED2KD_LOGERR("failed to remove sources from db (%s)", sqlite3_errmsg(g_db));
    return -1;
}

int db_search_file( struct search_node *snode, struct evbuffer *buf, size_t *count )
{
    int err;
    const char *tail;
    sqlite3_stmt *stmt = 0;
    size_t i;
    struct {
        char name_term[MAX_NAME_TERM_LEN+1];
        size_t name_len;
        uint64_t minsize;
        uint64_t maxsize;
        uint64_t srcavail;
        uint64_t srccomplete;
        uint64_t minbitrate;
        uint64_t minlength;
        struct search_node *ext_node;
        struct search_node *codec_node;
        struct search_node *type_node;
    } params;
    char query[MAX_SEARCH_QUERY_LEN+1] =
        " SELECT f.hash,f.name,f.size,f.type,f.ext,f.srcavail,f.srccomplete,f.rating,f.rated_count,"
        "  (SELECT sid FROM sources WHERE fid=f.fid LIMIT 1) AS sid,"
        "  f.mlength,f.mbitrate,f.mcodec "
        " FROM fnames n"
        " JOIN files f ON f.fid = n.docid"
        " WHERE fnames MATCH ?"
    ;

    memset(&params, 0, sizeof params);

    while ( snode ) {
        if ( (ST_AND <= snode->type) && (ST_NOT >= snode->type) ) {
            if ( !snode->left_visited ) {
                if ( snode->string_term ) {
                    params.name_len++;
                    DB_CHECK( params.name_len < sizeof params.name_term );
                    strcat(params.name_term, "(");
                }
                snode->left_visited = 1;
                snode = snode->left;
                continue;
            } else if ( !snode->right_visited ) {
                if ( snode->string_term ) {
                    const char *oper = 0;
                    switch( snode->type ) {
                    case ST_AND:
                        params.name_len += 5;
                        oper = " AND ";
                        break;
                    case ST_OR:
                        params.name_len += 4;
                        oper = " OR ";
                        break;
                    case ST_NOT:
                        params.name_len += 5;
                        oper = " NOT ";
                        break;

                    default:
                        DB_CHECK(0);
                    }
                    DB_CHECK( params.name_len < sizeof params.name_term );
                    strcat(params.name_term, oper);
                }
                snode->right_visited = 1;
                snode = snode->right;
                continue;
            } else {
                if ( snode->string_term ) {
                    params.name_len++;
                    DB_CHECK( params.name_len < sizeof params.name_term);
                    strcat(params.name_term, ")");
                }
            }
        } else {
            switch ( snode->type ) {
            case ST_STRING:
                params.name_len += snode->str_len;
                DB_CHECK( params.name_len < sizeof params.name_term );
                strncat(params.name_term, snode->str_val, snode->str_len);
                break;
            case ST_EXTENSION:
                params.ext_node = snode;
                break;
            case ST_CODEC:
                params.codec_node = snode;
                break;
            case ST_MINSIZE:
                params.minsize = snode->int_val;
                break;
            case ST_MAXSIZE:
                params.maxsize = snode->int_val;
                break;
            case ST_SRCAVAIL:
                params.srcavail = snode->int_val;
                break;
            case ST_SRCCOMLETE:
                params.srccomplete = snode->int_val;
                break;
            case ST_MINBITRATE:
                params.minbitrate = snode->int_val;
                break;
            case ST_MINLENGTH:
                params.minlength = snode->int_val;
                break;
            case ST_TYPE:
                params.type_node = snode;
                break;
            default:
                DB_CHECK(0);
            }
        }

        snode = snode->parent;
    }

    if ( params.ext_node ) {
        strcat(query, " AND f.ext=?");
    }
    if ( params.codec_node ) {
        strcat(query, " AND f.mcodec=?");
    }
    if ( params.minsize ) {
        strcat(query, " AND f.size>?");
    }
    if ( params.maxsize ) {
        strcat(query, " AND f.size<?");
    }
    if ( params.srcavail ) {
        strcat(query, " AND f.srcavail>?");
    }
    if ( params.srccomplete ) {
        strcat(query, " AND f.srccomplete>?");
    }
    if ( params.minbitrate ) {
        strcat(query, " AND f.mbitrate>?");
    }
    if ( params.minlength ) {
        strcat(query, " AND f.mlength>?");
    }
    if ( params.type_node ) {
        strcat(query, " AND f.type=?");
    }
    strcat(query, " LIMIT ?");

    DB_CHECK( SQLITE_OK == sqlite3_prepare_v2(g_db, query, params.name_len+1, &stmt, &tail) );

    i=1;
    DB_CHECK( SQLITE_OK == sqlite3_bind_text(stmt, i++, params.name_term, strlen(params.name_term)+1, SQLITE_STATIC) );

    if ( params.ext_node ) {
        DB_CHECK( SQLITE_OK == sqlite3_bind_text(stmt, i++, params.ext_node->str_val, params.ext_node->str_len, SQLITE_STATIC) );
    }
    if ( params.codec_node ) {
        DB_CHECK( SQLITE_OK == sqlite3_bind_text(stmt, i++, params.codec_node->str_val, params.codec_node->str_len, SQLITE_STATIC) );
    }
    if ( params.minsize ) {
        DB_CHECK( SQLITE_OK == sqlite3_bind_int64(stmt, i++, params.minsize) );
    }
    if ( params.maxsize ) {
        DB_CHECK( SQLITE_OK == sqlite3_bind_int64(stmt, i++, params.maxsize) );
    }
    if ( params.srcavail ) {
        DB_CHECK( SQLITE_OK == sqlite3_bind_int64(stmt, i++, params.srcavail) );
    }
    if ( params.srccomplete ) {
        DB_CHECK( SQLITE_OK == sqlite3_bind_int64(stmt, i++, params.srccomplete) );
    }
    if ( params.minbitrate ) {
        DB_CHECK( SQLITE_OK == sqlite3_bind_int64(stmt, i++, params.minbitrate) );
    }
    if ( params.minlength ) {
        DB_CHECK( SQLITE_OK == sqlite3_bind_int64(stmt, i++, params.minlength) );
    }
    if ( params.type_node ) {
        uint8_t type = get_ed2k_file_type(params.type_node->str_val, params.type_node->str_len);
        DB_CHECK( SQLITE_OK == sqlite3_bind_int(stmt, i++, type) );
    }

    DB_CHECK( SQLITE_OK == sqlite3_bind_int(stmt, i++, *count) );

    i = 0;
    while ( ((err = sqlite3_step(stmt)) == SQLITE_ROW) && (i < *count) ) {
        struct search_file sfile;
        uint64_t sid;
        int col = 0;

        memset(&sfile, 0, sizeof sfile);

        sfile.hash = (const unsigned char*)sqlite3_column_blob(stmt, col++);

        sfile.name_len = sqlite3_column_bytes(stmt, col);
        sfile.name_len = sfile.name_len > MAX_FILENAME_LEN ? MAX_FILENAME_LEN : sfile.name_len;
        sfile.name = (const char*)sqlite3_column_text(stmt, col++);

        sfile.size = sqlite3_column_int64(stmt, col++);
        sfile.type = sqlite3_column_int(stmt, col++);

        sfile.ext_len = sqlite3_column_bytes(stmt, col);
        sfile.ext_len = sfile.ext_len > MAX_FILEEXT_LEN ? MAX_FILEEXT_LEN : sfile.ext_len;
        sfile.ext = (const char*)sqlite3_column_text(stmt, col++);

        sfile.srcavail = sqlite3_column_int(stmt, col++);
        sfile.srccomplete = sqlite3_column_int(stmt, col++);
        sfile.rating = sqlite3_column_int(stmt, col++);
        sfile.rated_count = sqlite3_column_int(stmt, col++);

        sid = sqlite3_column_int64(stmt, col++);
        sfile.client_id = GET_SID_ID(sid);
        sfile.client_port = GET_SID_PORT(sid);

        sfile.media_length = sqlite3_column_int(stmt, col++);
        sfile.media_bitrate = sqlite3_column_int(stmt, col++);

        sfile.media_codec_len = sqlite3_column_bytes(stmt, col);
        sfile.media_codec_len = sfile.media_codec_len > MAX_FILEEXT_LEN ? MAX_FILEEXT_LEN : sfile.media_codec_len;
        sfile.media_codec = (const char*)sqlite3_column_text(stmt, col++);

        write_search_file(buf, &sfile);

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

int db_get_sources( const unsigned char *hash, file_source_t *sources, uint8_t *count )
{
    sqlite3_stmt *stmt;
    const char *tail;
    uint8_t i;
    int err;
    static const char query[] =
        "SELECT sid FROM sources WHERE fid=? LIMIT ?";

    DB_CHECK( SQLITE_OK == sqlite3_prepare_v2(g_db, query, sizeof query, &stmt, &tail) );
    DB_CHECK( SQLITE_OK == sqlite3_bind_int64(stmt, 1, MAKE_FID(hash)) );
    DB_CHECK( SQLITE_OK == sqlite3_bind_int(stmt, 2, *count) );

    i=0;
    while ( ((err = sqlite3_step(stmt)) == SQLITE_ROW) && (i < *count) ) {
        uint64_t sid = sqlite3_column_int64(stmt, 0);
        sources[i].ip = GET_SID_ID(sid);
        sources[i].port = GET_SID_PORT(sid);
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
