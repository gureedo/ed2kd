QT -= core gui
TARGET = ed2kd
CONFIG += console
CONFIG -= app_bundle
TEMPLATE = app

QMAKE_CFLAGS += -std=gnu99
LIBS += -L"$$_PRO_FILE_PWD_/3rdparty/libs/" -lconfig -levent_core -lws2_32

CONFIG(debug, debug|release) {
DEFINES += DEBUG
}

INCLUDEPATH += 3rdparty/libevent/include \
    3rdparty/libevent/WIN32-Code \
    3rdparty/libconfig/lib

SOURCES += \
    src/config.c \
    src/log.c \
    src/protofilter.c \
    src/main.c \
    src/port_check.c \
    src/client.c \
    src/ed2kd.c \
    src/util.c

HEADERS  += \
    src/config.h \
    src/client.h \
    src/log.h \
    src/util.h \
    src/protofilter.h \
    src/port_check.h \
    src/packet_buffer.h \
    src/ed2k_proto.h \
    src/version.h \
    src/ed2kd.h


