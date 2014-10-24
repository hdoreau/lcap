AM_CFLAGS = -Wall -Werror -fPIC

include_HEADERS=lcap_client.h

lib_LTLIBRARIES=liblcap.la

liblcap_la_SOURCES=client.c lu_client.c px_client.c lcap_client.h
liblcap_la_LIBADD=../common/liblcapcommon.la
liblcap_la_LDFLAGS=-ldl -lzmq -version-number @lcap_lib_version@
