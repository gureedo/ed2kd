cmake_minimum_required (VERSION 2.6)
project (ed2kd)

OPTION(BACKEND_SQLITE3 "Use sqlite3 database" OFF)

SET(SRCS client.c config.c ed2kd.c log.c main.c portcheck.c util.c)


add_executable(ed2kd $(SRCS))