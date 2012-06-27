QT -= core gui
TARGET = ed2kd
CONFIG += console
CONFIG -= app_bundle
TEMPLATE = app

CONFIG(debug, debug|release) {
DEFINES += DEBUG
}

SOURCES += \
    src/protofilter.c \
    src/port_check.c \
    src/util.c \
    src/portcheck.c \
    src/main.c \
    src/log.c \
    src/ed2kd.c \
    src/config.c \
    src/client.c

HEADERS  += \
    src/protofilter.h \
    src/port_check.h \
    src/version.h \
    src/util.h \
    src/portcheck.h \
    src/packet_buffer.h \
    src/packed_struct.h \
    src/log.h \
    src/ed2kd.h \
    src/ed2k_proto.h \
    src/db.h \
    src/config.h \
    src/client.h


