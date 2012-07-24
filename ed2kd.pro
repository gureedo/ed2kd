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
    src/portcheck.c \
    src/main.c \
    src/log.c \
    src/config.c \
    src/client.c \
    src/util.c \
    src/db_sqlite.c \
    src/event_callback.c \
    src/server.c

HEADERS  += \
    src/packet_buffer.h \
    src/packed_struct.h \
    src/log.h \
    src/ed2k_proto.h \
    src/db.h \
    src/config.h \
    src/client.h \
    src/util.h \
    src/portcheck.h \
    src/version.h \
    src/event_callback.h \
    src/server.h \
    src/job.h \
    src/queue.h
