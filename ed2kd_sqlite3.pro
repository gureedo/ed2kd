QT -= core gui
TARGET = ed2kd
CONFIG += console
CONFIG -= app_bundle
TEMPLATE = app

LIBS += -lconfig -levent_core -levent_pthreads -lz -lm
QMAKE_LFLAGS += -fopenmp

CONFIG(debug, debug|release) {
DEFINES += USE_DEBUG
}

DEFINES += SQLITE_THREADSAFE=1 \
        SQLITE_ENABLE_FTS4 \
        SQLITE_ENABLE_FTS3_PARENTHESIS \
        SQLITE_ENABLE_FTS4_UNICODE61 \
        SQLITE_OMIT_LOAD_EXTENSION

SOURCES += \
        src/client.c \
        src/config.c \
        src/db_sqlite.c \
        src/job.c \
        src/log.c \
        src/main.c \
        src/packet.c \
        src/portcheck.c \
        src/server.c \
        src/server_listener.c \
        src/sqlite3/sqlite3.c \
        src/util.c

HEADERS  += \
        src/atomic.h \
        src/client.h \
        src/config.h \
        src/db.h \
        src/ed2k_proto.h \
        src/job.h \
        src/log.h \
        src/login.h \
        src/packed_struct.h \
        src/packet.h \
        src/portcheck.h \
        src/queue.h \
        src/server.h \
        src/uthash.h \
        src/util.h \
        src/version.h
