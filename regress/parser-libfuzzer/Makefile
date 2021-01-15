#	$OpenBSD: Makefile,v 1.2 2017/05/30 15:36:13 bluhm Exp $

WARNINGS=Yes

# libfuzzer needs clang
CC = clang

PROG=		test_parser
SRCS=		common.c test_parser_fuzz.c
IKEOBJS=	ikev2_pld.o imsg_util.o log.o util.o \
		ikev2_map.o eap_map.o
CLEANFILES=	${IKEOBJS}

LDADD+=		-lutil -lcrypto ${IKEOBJS} -fsanitize=fuzzer
DPADD+=		${LIBUTIL} ${LIBCRYPTO}
CFLAGS+=	-I${.CURDIR}/../../../../sbin/iked
CFLAGS+=	-g -O1 -fsanitize=fuzzer
CFLAGS+=	-Wno-missing-field-initializers

test_parser: ${IKEOBJS}

${IKEOBJS}:
	cd ${.CURDIR}/../../../../sbin/iked && make $@
	ln -sf ${.OBJDIR}/../../../../sbin/iked/$@ .

LDADD+=		-L${.OBJDIR}

.include <bsd.regress.mk>
