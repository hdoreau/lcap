AM_CFLAGS=$(CC_OPT)

lib_LTLIBRARIES=liblcap.la

liblcap_la_SOURCES=client.c lu_client.c px_client.c
liblcap_la_LIBADD=../common/liblcapcommon.la
liblcap_la_LDFLAGS=-ldl -lzmq -version-number @lcap_lib_version@
