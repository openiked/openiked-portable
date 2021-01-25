# Copyright (c) 2020-2021 Tobias Heider <tobhe@openbsd.org>
#
# Permission to use, copy, modify, and distribute this software for any
# purpose with or without fee is hereby granted, provided that the above
# copyright notice and this permission notice appear in all copies.
#
# THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
# WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
# MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
# ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
# WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
# ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
# OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

include(CheckFunctionExists)
include(CheckLibraryExists)
include(CheckSymbolExists)
include(CheckIncludeFiles)

set(VERSIONED_FILES)
list(APPEND VERSIONED_FILES iked.c)

set(IKED_COMPAT		"${CMAKE_CURRENT_SOURCE_DIR}/../iked/compat")
set(SRCS)
set(CSRCS)

if(NOT HAVE_RECALLOCARRAY)
	list(APPEND CSRCS ${IKED_COMPAT}/recallocarray.c)
endif()
if(NOT HAVE_SOCK_NONBLOCK)
	list(APPEND CSRCS ${IKED_COMPAT}/bsd-socket.c)
endif()
if(NOT HAVE_SETRESUID)
	list(APPEND CSRCS ${IKED_COMPAT}/bsd-setres_id.c)
endif()
if(NOT HAVE_GETRTABLE)
	list(APPEND CSRCS ${IKED_COMPAT}/getrtable.c)
endif()
if(NOT HAVE_GETDTABLECOUNT)
	list(APPEND CSRCS ${IKED_COMPAT}/getdtablecount.c)
endif()
if(NOT HAVE_SETPROCTITLE)
	list(APPEND CSRCS ${IKED_COMPAT}/setproctitle.c)
endif()
if(NOT HAVE_STRTONUM)
	list(APPEND CSRCS ${IKED_COMPAT}/strtonum.c)
endif()
if(NOT HAVE_FFS)
	list(APPEND CSRCS ${IKED_COMPAT}/ffs.c)
endif()
if(NOT HAVE_GETOPT)
	list(APPEND CSRCS ${IKED_COMPAT}/getopt_long.c)
endif()
if(NOT HAVE_IMSG_H)
	list(APPEND CSRCS
		# imsg
		${IKED_COMPAT}/imsg.c
		${IKED_COMPAT}/imsg-buffer.c
	)
endif()
if(NOT HAVE_STRLCPY)
	list(APPEND CSRCS ${IKED_COMPAT}/strlcpy.c)
endif()
if(NOT HAVE_STRLCAT)
	list(APPEND CSRCS ${IKED_COMPAT}/strlcat.c)
endif()
if(NOT HAVE_FREEZERO)
	list(APPEND CSRCS ${IKED_COMPAT}/freezero.c)
endif()
if(NOT HAVE_ARC4RANDOM_BUF)
	list(APPEND CSRCS ${IKED_COMPAT}/arc4random.c)
endif()
if(NOT HAVE_ARC4RANDOM_UNIFORM)
	list(APPEND CSRCS ${IKED_COMPAT}/arc4random_uniform.c)
endif()
if(NOT HAVE_EXPLICIT_BZERO)
	list(APPEND CSRCS ${IKED_COMPAT}/explicit_bzero.c)
endif()
if(NOT HAVE_REALLOCARRAY)
	list(APPEND CSRCS ${IKED_COMPAT}/reallocarray.c)
endif()
if(NOT HAVE_VIS)
	list(APPEND CSRCS ${IKED_COMPAT}/vis.c)
endif()

set(CFLAGS)
list(APPEND CFLAGS
	-O2
	-Wall
	-Wno-pointer-sign
	-Wno-deprecated-declarations
	-Wstrict-prototypes
	-Wmissing-prototypes
	-Wmissing-declarations
	-Wshadow
	-Wpointer-arith
	-Wcast-qual
	-Wsign-compare
	"$<$<CONFIG:DEBUG>:-O0;-g>"
)

set(INC_DIRS
	${IKED_COMPAT}
	${CMAKE_CURRENT_SOURCE_DIR}
	${CMAKE_CURRENT_SOURCE_DIR}/../iked
)

if(CSRCS)
	add_library(compat OBJECT ${CSRCS})
	target_compile_options(compat PRIVATE ${CFLAGS})
	target_include_directories(compat PRIVATE ${INC_DIRS})
endif()

add_library(iked-shared OBJECT
	ikev2_pld.c
	imsg_util.c
	log.c
	util.c
	${CMAKE_CURRENT_BINARY_DIR}/ikev2_map.c
	${CMAKE_CURRENT_BINARY_DIR}/eap_map.c
)

if(CMAKE_SYSTEM_NAME MATCHES "OpenBSD")
	list(APPEND SRCS ipsec.c pfkey.c)
elseif(CMAKE_SYSTEM_NAME MATCHES "Linux")
	list(APPEND SRCS ipsec.c pfkey.c)
elseif(CMAKE_SYSTEM_NAME MATCHES "Darwin")
	list(APPEND SRCS ipsec-linux.c)
elseif(CMAKE_SYSTEM_NAME MATCHES "FreeBSD")
	list(APPEND SRCS ipsec.c pfkey.c)
endif()

list(APPEND SRCS
	ca.c
	chap_ms.c
	config.c
	control.c
	crypto.c
	dh.c
	eap.c
	iked.c
	ikev2.c
	ikev2.h
	ikev2_msg.c
	ocsp.c
	policy.c
	proc.c
	smult_curve25519_ref.c
	timer.c
	# Generated files
	${CMAKE_CURRENT_BINARY_DIR}/parse.c
	$<TARGET_OBJECTS:iked-shared>
)

if(TARGET compat)
	list(APPEND SRCS $<TARGET_OBJECTS:compat>)
endif()

add_executable(iked ${SRCS})

target_compile_options(iked PRIVATE ${CFLAGS})
target_compile_options(iked-shared PRIVATE ${CFLAGS})

target_include_directories(iked PRIVATE ${INC_DIRS})
target_include_directories(iked-shared PRIVATE ${INC_DIRS})

target_link_libraries(iked
	PRIVATE util event crypto ssl
)
target_link_libraries(iked-shared
	PRIVATE util event crypto ssl
)

add_custom_command(
	OUTPUT parse.c
	COMMAND yacc -o parse.c ${CMAKE_CURRENT_SOURCE_DIR}/parse.y
	DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/parse.y
)

add_custom_command(
	OUTPUT ikev2_map.c
	COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/genmap.sh
	    ${CMAKE_CURRENT_SOURCE_DIR}/ikev2.h ikev2 > ikev2_map.c
	DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/ikev2.h
	DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/genmap.sh
)

add_custom_command(
	OUTPUT eap_map.c
	COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/genmap.sh
	    ${CMAKE_CURRENT_SOURCE_DIR}/eap.h eap > eap_map.c
	DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/eap.h
	DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/genmap.sh
)

install(TARGETS iked RUNTIME DESTINATION sbin)
install(FILES ${CMAKE_SOURCE_DIR}/iked.conf DESTINATION ${CMAKE_INSTALL_SYSCONFDIR})
install(DIRECTORY DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/iked/ca)
install(DIRECTORY DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/iked/certs)
install(DIRECTORY DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/iked/crls)
install(DIRECTORY DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/iked/private)
install(DIRECTORY DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/iked/pubkeys/ipv4)
install(DIRECTORY DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/iked/pubkeys/ipv6)
install(DIRECTORY DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/iked/pubkeys/fqdn)
install(DIRECTORY DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/iked/pubkeys/ufqdn)
