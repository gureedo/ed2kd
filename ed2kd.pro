QT -= core gui
TARGET = ed2kd
CONFIG += console
CONFIG -= app_bundle
TEMPLATE = app

LIBS += -lconfig -levent_core -levent_pthreads -lsqlite3 -lz -latomic_ops -lm
QMAKE_LFLAGS += -fopenmp

CONFIG(debug, debug|release) {
DEFINES += DEBUG
}

SOURCES += \
    src/util.c \
    src/server.c \
    src/portcheck.c \
    src/packet.c \
    src/main.c \
    src/log.c \
    src/event_callback.c \
    src/db_sqlite.c \
    src/config.c \
    src/client.c

HEADERS  += \
    src/packet_buffer.h \
    src/version.h \
    src/util.h \
    src/uthash.h \
    src/server.h \
    src/queue.h \
    src/portcheck.h \
    src/packet.h \
    src/packed_struct.h \
    src/log.h \
    src/job.h \
    src/event_callback.h \
    src/ed2k_proto.h \
    src/db.h \
    src/config.h \
    src/client.h
