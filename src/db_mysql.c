#include "db.h"
#ifdef _WIN32
#include <winsock2.h>
#endif
#include <mysql.h>
#include "util.h"
#include "log.h"
#include "ed2k_proto.h"
#include "client.h"
#include "server.h"

#define DB_CHECK(x)         if (!(x)) goto failed;

enum query_statements {
        STMT_ADD_FILE,
        STMT_ADD_SRC,
        STMT_DEL_SRC,
        STMT_GET_SRC,
        STMT_COUNT
};

static THREAD_LOCAL MYSQL *s_db;
static THREAD_LOCAL MYSQL_STMT *s_stmt[STMT_COUNT];

int db_create()
{
        const char query[] = 
                "DELETE FROM `sources`;"
                "UPDATE `files` SET `srvavail`=0,`srccomplete`=0,`rating`=0,`rated_count`=0;"
        ;

        if ( mysql_library_init(0, NULL, NULL) ) {
                ED2KD_LOGERR("could not initialize MySQL library\n");
                return -1;
        }

        if ( !mysql_thread_safe() ) {
                ED2KD_LOGERR("MySQL library is not threadsafe");
                return -1;
        }

        s_db = mysql_init(NULL);
        if ( !s_db ) {
                ED2KD_LOGERR("mysql_init() failed");
                return -1;
        }

        if ( mysql_real_connect(s_db, g_srv.cfg->db_host, g_srv.cfg->db_user, 
             g_srv.cfg->db_password, g_srv.cfg->db_schema, g_srv.cfg->db_port, 
             g_srv.cfg->db_unixsock, CLIENT_MULTI_STATEMENTS) ) {
                ED2KD_LOGERR("failed connect to database: %s", mysql_error(s_db));
                return -1;
        }

        DB_CHECK( !mysql_real_query(s_db, query, sizeof(query)-1) );

        return 0;

failed:
        ED2KD_LOGERR("MySQL error: %s", mysql_error(s_db));
        return -1;
}

int db_destroy()
{
        mysql_close(s_db);
        
        mysql_library_end();

        return 0;
}

int db_open()
{
        static const char query_add_file[] =
                "INSERT INTO files(fid,name,ext,size,type,mlength,mbitrate,mcodec)"
                "  VALUES(?,?,?,?,?,?,?,?,?)"
                "ON DUPLICATE KEY UPDATE"
                "  name=VALUES(name), ext=VALUES(ext), size=VALUES(size), type=VALUES(type),"
                "  mlength=VALUES(mlength), mbitrate=VALUES(mbitrate), mcodec=VALUES(mcodec);";
        static const char query_add_src[] =
                "INSERT INTO sources(fid,src_id,src_port,complete,rating) VALUES(?,?,?,?,?)";
        static const char query_del_src[] =
                "DELETE FROM sources WHERE src_id=? AND src_port=?";
        static const char query_get_src[] =
                "SELECT src_id,src_port FROM sources WHERE fid=? LIMIT ?";

        s_db = mysql_init(NULL);
        if ( !s_db ) {
                ED2KD_LOGERR("mysql_init() failed");
                return -1;
        }

        if ( mysql_real_connect(s_db, g_srv.cfg->db_host, g_srv.cfg->db_user, 
             g_srv.cfg->db_password, g_srv.cfg->db_schema, g_srv.cfg->db_port, 
             g_srv.cfg->db_unixsock, 0) ) {
                ED2KD_LOGERR("failed connect to database: %s", mysql_error(s_db));
                return -1;
        }

        s_stmt[STMT_ADD_FILE] = mysql_stmt_init(s_db);
        DB_CHECK( !mysql_stmt_prepare(s_stmt[STMT_ADD_FILE], query_add_file, sizeof(query_add_file)-1) );
        s_stmt[STMT_ADD_SRC] = mysql_stmt_init(s_db);
        DB_CHECK( !mysql_stmt_prepare(s_stmt[STMT_ADD_SRC], query_add_src, sizeof(query_add_src)-1) );
        s_stmt[STMT_DEL_SRC] = mysql_stmt_init(s_db);
        DB_CHECK( !mysql_stmt_prepare(s_stmt[STMT_DEL_SRC], query_del_src, sizeof(query_del_src)-1) );
        s_stmt[STMT_GET_SRC] = mysql_stmt_init(s_db);
        DB_CHECK( !mysql_stmt_prepare(s_stmt[STMT_GET_SRC], query_get_src, sizeof(query_get_src)-1) );

        return 0;

failed:
        db_close();

        return -1;
}

int db_close()
{
        int i;

        for ( i=0; i<STMT_COUNT; ++i ) {
                if ( s_stmt[i] )
                        mysql_stmt_close(s_stmt[i]);
        }

        mysql_close(s_db);

        return 0;
}

int db_share_files( const struct pub_file *files, size_t count, const struct client *owner )
{
        int i;
        const char *ext;
        size_t ext_len;
        my_bool is_ext_null, is_codec_null;
        MYSQL_STMT *stmt_file = s_stmt[STMT_ADD_FILE], *stmt_src = s_stmt[STMT_ADD_SRC];
        MYSQL_BIND bind_file[8], bind_src[5];

        memset(bind, 0, sizeof(bind_file));
        i=0;
        /* fid */
        bind_file[i].buffer_type = MYSQL_TYPE_STRING;
        bind_file[i].buffer_length = ED2K_HASH_SIZE;
        bind_file[i].length = &bind_file[i].buffer_length;
        /* name */
        i++;
        bind_file[i].buffer_type = MYSQL_TYPE_STRING;
        bind_file[i].length = &bind_file[i].buffer_length;
        /* ext */
        i++;
        bind_file[i].buffer_type = MYSQL_TYPE_STRING;
        bind_file[i].buffer = (void*)ext;
        bind_file[i].length = &bind_file[i].buffer_length;
        bind_file[i].is_null = &is_ext_null;
        /* size */
        i++;
        bind_file[i].buffer_type = MYSQL_TYPE_LONGLONG;
        bind_file[i].is_unsigned = 1;
        /* type */
        i++;
        bind_file[i].buffer_type = MYSQL_TYPE_TINY;
        bind_file[i].is_unsigned = 1;
        /* mlength */
        i++;
        bind_file[i].buffer_type = MYSQL_TYPE_LONG;
        bind_file[i].is_unsigned = 1;
        /* mbitrate */
        i++;
        bind_file[i].buffer_type = MYSQL_TYPE_LONG;
        bind_file[i].is_unsigned = 1;
        /* mcodec */
        i++;
        bind_file[i].buffer_type = MYSQL_TYPE_STRING;
        bind_file[i].length = &bind_file[i].buffer_length;
        bind_file[i].is_null = &is_codec_null;

        i=0;
        /* fid */
        bind_src[i].buffer_type = MYSQL_TYPE_STRING;
        bind_src[i].buffer_length = ED2K_HASH_SIZE;
        bind_src[i].length = &bind_src[i].buffer_length;
        /* src_id */
        i++;
        bind_src[i].buffer_type = MYSQL_TYPE_LONG;
        bind_src[i].is_unsigned = 1;
        /* src_port */
        i++;
        bind_src[i].buffer_type = MYSQL_TYPE_SHORT;
        bind_src[i].is_unsigned = 1;
        /* complete */
        i++;
        bind_src[i].buffer_type = MYSQL_TYPE_TINY;
        bind_src[i].is_unsigned = 1;
        /* rating */
        i++;
        bind_src[i].buffer_type = MYSQL_TYPE_LONG;
        bind_src[i].is_unsigned = 1;

        while ( count-- > 0 ) {
                if ( !files->name_len ) {
                        files++;
                        continue;
                }

                /* find extension */
                ext = file_extension(files->name, files->name_len);
                if ( ext )
                        ext_len  = files->name + files->name_len - ext;

                DB_CHECK( !mysql_stmt_reset(stmt_file) );
                i=0;

                /* fid */
                bind_file[i].buffer = (void*)files->hash;
                /* name */
                i++;
                bind_file[i].buffer = (void*)files->name;
                bind_file[i].buffer_length = files->name_len;
                /* ext */
                i++;
                is_ext_null = ext ? 0 : 1;
                bind_file[i].buffer_length = ext_len;
                /* size */
                i++;
                bind_file[i].buffer = (void*)&files->size;
                /* type */
                i++;
                bind_file[i].buffer = (void*)&files->type;
                /* mlength */
                i++;
                bind_file[i].buffer = (void*)&files->media_length;
                /* mbitrate */
                i++;
                bind_file[i].buffer = (void*)&files->media_bitrate;
                /* mcodec */
                i++;
                is_codec_null = files->media_codec_len ? 0 : 1;
                bind_file[i].buffer = (void*)files->media_codec;
                bind_file[i].buffer_length = files->media_codec_len;
                DB_CHECK( !mysql_stmt_bind_param(stmt_file, bind_file) );
                DB_CHECK( !mysql_stmt_execute(stmt_file) );

                DB_CHECK( !mysql_stmt_reset(stmt_src) );
                i=0;
                
                /* fid */
                i++;
                bind_src[i].buffer = (void*)files->hash;
                /* src_id */
                i++;
                bind_src[i].buffer = (void*)&owner->id;
                /* src_port */
                i++;
                bind_src[i].buffer = (void*)&owner->port;
                /* complete */
                i++;
                bind_src[i].buffer = (void*)&files->complete;
                /* rating */
                i++;
                bind_src[i].buffer = (void*)&files->rating;

                DB_CHECK( !mysql_stmt_bind_param(stmt_src, bind_src) );
                DB_CHECK( !mysql_stmt_execute(stmt_src) );

                files++;
        }

        return 0;

failed:
        ED2KD_LOGERR("failed to add file to db (%s)", mysql_error(s_db));
        return -1;
}

int db_remove_source( const struct client *clnt )
{
        int i;
        MYSQL_STMT *stmt = s_stmt[STMT_DEL_SRC];
        MYSQL_BIND bind_del[2];

        memset(bind_del, 0, sizeof(bind_del));
        
        DB_CHECK( !mysql_stmt_reset(stmt) );

        i=0;
        /* src_id */
        bind_del[i].buffer_type = MYSQL_TYPE_LONG;
        bind_del[i].is_unsigned = 1;
        bind_del[i].buffer = (void*)&clnt->id;
        /* src_port */
        i++;
        bind_del[i].buffer_type = MYSQL_TYPE_SHORT;
        bind_del[i].is_unsigned = 1;
        bind_del[i].buffer = (void*)&clnt->port;

        DB_CHECK( !mysql_stmt_bind_param(stmt, bind_del) );
        DB_CHECK( !mysql_stmt_execute(stmt) );

        return 0;

failed:
        ED2KD_LOGERR("failed to remove sources from db (%s)", mysql_error(s_db));
        return -1;
}

int db_search_files( struct search_node *snode, struct evbuffer *buf, size_t *count )
{
}

int db_get_sources( const unsigned char *hash, struct file_source *sources, uint8_t *count )
{
        size_t i;
        MYSQL_STMT *stmt = s_stmt[STMT_GET_SRC];
        MYSQL_BIND bind_get[2], bind_res[2];
        uint32_t id;
        uint16_t port;

        memset(bind_get, 0, sizeof(bind_get));
        memset(bind_res, 0, sizeof(bind_res));

        DB_CHECK( !mysql_stmt_reset(stmt) );

        i = 0;
        /* fid */
        bind_get[i].buffer_type = MYSQL_TYPE_STRING;
        bind_get[i].buffer_length = ED2K_HASH_SIZE;
        bind_get[i].length = &bind_get[i].buffer_length;
        bind_get[i].buffer = (void *)hash;
        /* limit */
        i++;
        bind_get[i].buffer_type = MYSQL_TYPE_TINY;
        bind_get[i].is_unsigned = 1;
        bind_get[i].buffer = (void *)count;

        DB_CHECK( !mysql_stmt_bind_param(stmt, bind_get) );
        DB_CHECK( !mysql_stmt_execute(stmt) );
        DB_CHECK( !mysql_stmt_store_result(stmt) );

        i=0;
        /* src_id */
        bind_res[i].buffer_type = MYSQL_TYPE_LONG;
        bind_res[i].buffer = (void *)&id;
        bind_res[i].is_unsigned = 1;
        bind_res[i].buffer_length = sizeof(id);
        /* src_port */
        i++;
        bind_res[i].buffer_type = MYSQL_TYPE_SHORT;
        bind_res[i].buffer = (void *)&port;
        bind_res[i].is_unsigned = 1;
        bind_res[i].buffer_length = sizeof(port);

        DB_CHECK( !mysql_stmt_bind_result(stmt, bind_res) );

        i=0;
        while ( !mysql_stmt_fetch(stmt) && (i < *count) ) {
                sources[i].ip = id;
                sources[i].port = port;
                ++i;
        }

        *count = i;
        return 0;

failed:
        ED2KD_LOGERR("failed to get sources from db (%s)", mysql_stmt_error(stmt));
        return -1;
}