AM_CFLAGS = -Wall -Werror

include_HEADERS=lcap.h

sbin_PROGRAMS=lcapd
#lcapd_CFLAGS=
lcapd_LDFLAGS=-static
lcapd_LDADD=../common/liblcapcommon.la -lzmq -ldl -llustreapi -lpthread

lcapd_SOURCES=	lcapd.c \
		config.c \
		modules.c \
		modules.h \
		reader.c \
		utils.c \
		worker.c \
		lcapd_internal.h \
		lcap.h \
		modules.h \
		utils.h
