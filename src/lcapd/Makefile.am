AM_CFLAGS = -Wall -Werror

include_HEADERS=lcap.h

sbin_PROGRAMS=lcapd
#lcapd_CFLAGS=
lcapd_LDFLAGS=-static
lcapd_LDADD=../common/liblcapcommon.la ../lcapnet/liblcapnet.la -lzmq -ldl -llustreapi -lpthread

lcapd_SOURCES=	lcapd.c \
		config.c \
		reader.c \
		broker.c \
		rpc_utils.c \
		lcapd_internal.h \
		lcap.h
