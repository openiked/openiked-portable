# Copyright (c) 2020 Tobias Heider <tobhe@openbsd.org>
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

#cmake_minimum_required(VERSION 3.0)

#include(CheckCCompilerFlag)
include(CheckFunctionExists)
include(CheckLibraryExists)
include(CheckSymbolExists)
include(CheckIncludeFiles)
#include(CheckTypeSize)
#include(GNUInstallDirs)

set(IKED_COMPAT		"${CMAKE_CURRENT_SOURCE_DIR}/../iked/compat")

set(SRCS)

if(CMAKE_SYSTEM_NAME MATCHES "OpenBSD")
	add_definitions(-DHAVE_ATTRIBUTE__BOUNDED__)
	add_definitions(-DHAVE_ATTRIBUTE__DEAD__)
	add_definitions(-DHAVE_SOCKADDR_SA_LEN)
	add_definitions(-DHAVE_FFS)
endif()

if(CMAKE_BUILD_TYPE STREQUAL DEBUG)
	message("Debug build.")
elseif(CMAKE_BUILD_TYPE STREQUAL RELEASE)
	message("Release build.")
endif()

check_include_files("sys/types.h;sys/queue.h;imsg.h" HAVE_IMSG_H)
if(HAVE_IMSG_H)
	add_definitions(-DHAVE_IMSG_H)
endif()

check_include_files(unistd.h HAVE_UNISTD_H)
if(HAVE_UNISTD_H)
	add_definitions(-DHAVE_UNISTD_H)
endif()

check_function_exists(recallocarray HAVE_RECALLOCARRAY)
if(HAVE_RECALLOCARRAY)
	add_definitions(-DHAVE_RECALLOCARRAY)
endif()

check_function_exists(reallocarray HAVE_REALLOCARRAY)
if(HAVE_REALLOCARRAY)
	add_definitions(-DHAVE_REALLOCARRAY)
endif()

check_function_exists(accept4 HAVE_ACCEPT4)
if(HAVE_ACCEPT4)
	add_definitions(-DHAVE_ACCEPT4)
endif()

check_symbol_exists(SOCK_NONBLOCK "sys/socket.h" HAVE_SOCK_NONBLOCK)
if(HAVE_SOCK_NONBLOCK)
	add_definitions(-DHAVE_SOCK_NONBLOCK)
endif()

check_function_exists(setproctitle HAVE_SETPROCTITLE)
if(HAVE_SETPROCTITLE)
	add_definitions(-DHAVE_SETPROCTITLE)
endif()

check_function_exists(pledge HAVE_PLEDGE)
if(HAVE_PLEDGE)
	add_definitions(-DHAVE_PLEDGE)
endif()

check_function_exists(setresgid HAVE_SETRESGID)
if(HAVE_SETRESGID)
	add_definitions(-DHAVE_SETRESGID)
endif()

check_function_exists(setresuid HAVE_SETRESUID)
if(HAVE_SETRESUID)
	add_definitions(-DHAVE_SETRESUID)
endif()

check_function_exists(setregid HAVE_SETREGID)
if(HAVE_SETREGID)
	add_definitions(-DHAVE_SETREGID)
endif()

check_function_exists(setreuid HAVE_SETREUID)
if(HAVE_SETREUID)
	add_definitions(-DHAVE_SETREUID)
endif()

check_function_exists(getrtable HAVE_GETRTABLE)
if(HAVE_GETRTABLE)
	add_definitions(-DHAVE_GETRTABLE)
endif()

# setrtable
check_function_exists(setrtable HAVE_SETRTABLE)
if(HAVE_SETRTABLE)
	add_definitions(-DHAVE_SETRTABLE)
endif()

check_function_exists(strtonum HAVE_STRTONUM)
if(HAVE_STRTONUM)
	add_definitions(-DHAVE_STRTONUM)
endif()

check_symbol_exists(ifgroupreq "net/if.h" HAVE_IFGROUPREQ)
if(HAVE_IFGROUPREQ)
	add_definitions(-DHAVE_IFGROUPREQ)
endif()

check_function_exists(freezero HAVE_FREEZERO)
if(HAVE_FREEZERO)
	add_definitions(-DHAVE_FREEZERO)
endif()

check_function_exists(getdtablecount HAVE_GETDTABLECOUNT)
if(HAVE_GETDTABLECOUNT)
	add_definitions(-DHAVE_GETDTABLECOUNT)
endif()

check_function_exists(clock_gettime HAVE_CLOCK_GETTIME)
if(HAVE_CLOCK_GETTIME)
	add_definitions(-DHAVE_CLOCK_GETTIME)
endif()

check_symbol_exists(timespecsub "sys/time.h" HAVE_TIMESPECSUB)
if(HAVE_TIMESPECSUB)
	add_definitions(-DHAVE_TIMESPECSUB)
endif()

check_function_exists(asprintf HAVE_ASPRINTF)
if(HAVE_ASPRINTF)
	add_definitions(-DHAVE_ASPRINTF)
endif()

check_function_exists(reallocarray HAVE_REALLOCARRAY)
if(HAVE_REALLOCARRAY)
	add_definitions(-DHAVE_REALLOCARRAY)
endif()

check_function_exists(strcasecmp HAVE_STRCASECMP)
if(HAVE_STRCASECMP)
	add_definitions(-DHAVE_STRCASECMP)
endif()

check_function_exists(strlcat HAVE_STRLCAT)
if(HAVE_STRLCAT)
	add_definitions(-DHAVE_STRLCAT)
endif()

check_function_exists(strlcpy HAVE_STRLCPY)
if(HAVE_STRLCPY)
	add_definitions(-DHAVE_STRLCPY)
endif()

check_function_exists(strndup HAVE_STRNDUP)
if(HAVE_STRNDUP)
	add_definitions(-DHAVE_STRNDUP)
endif()

check_function_exists(strndup HAVE_FFS)
if(HAVE_FFS)
	add_definitions(-DHAVE_FFS)
endif()

check_function_exists(strnlen HAVE_STRNLEN)
if(HAVE_STRNLEN)
	add_definitions(-DHAVE_STRNLEN)
endif()

check_function_exists(strsep HAVE_STRSEP)
if(HAVE_STRSEP)
	add_definitions(-DHAVE_STRSEP)
endif()

check_function_exists(timegm HAVE_TIMEGM)
if(HAVE_TIMEGM)
	add_definitions(-DHAVE_TIMEGM)
endif()

check_function_exists(arc4random_buf HAVE_ARC4RANDOM_BUF)
if(HAVE_ARC4RANDOM_BUF)
	add_definitions(-DHAVE_ARC4RANDOM_BUF)
endif()

check_function_exists(arc4random_uniform HAVE_ARC4RANDOM_UNIFORM)
if(HAVE_ARC4RANDOM_UNIFORM)
	add_definitions(-DHAVE_ARC4RANDOM_UNIFORM)
endif()

check_function_exists(explicit_bzero HAVE_EXPLICIT_BZERO)
if(HAVE_EXPLICIT_BZERO)
	add_definitions(-DHAVE_EXPLICIT_BZERO)
endif()

check_function_exists(getauxval HAVE_GETAUXVAL)
if(HAVE_GETAUXVAL)
	add_definitions(-DHAVE_GETAUXVAL)
endif()

check_function_exists(getentropy HAVE_GETENTROPY)
if(HAVE_GETENTROPY)
	add_definitions(-DHAVE_GETENTROPY)
endif()

check_symbol_exists(getpagesize unistd.h HAVE_GETPAGESIZE)
if(HAVE_GETPAGESIZE)
	add_definitions(-DHAVE_GETPAGESIZE)
endif()

check_function_exists(getprogname HAVE_GETPROGNAME)
if(HAVE_GETPROGNAME)
	add_definitions(-DHAVE_GETPROGNAME)
endif()

check_function_exists(syslog_r HAVE_SYSLOG_R)
if(HAVE_SYSLOG_R)
	add_definitions(-DHAVE_SYSLOG_R)
endif()

check_function_exists(syslog HAVE_SYSLOG)
if(HAVE_SYSLOG)
	add_definitions(-DHAVE_SYSLOG)
endif()

check_symbol_exists(timespecsub sys/time.h HAVE_TIMESPECSUB)
if(HAVE_TIMESPECSUB)
	add_definitions(-DHAVE_TIMESPECSUB)
endif()

check_function_exists(timingsafe_bcmp HAVE_TIMINGSAFE_BCMP)
if(HAVE_TIMINGSAFE_BCMP)
	add_definitions(-DHAVE_TIMINGSAFE_BCMP)
endif()

check_function_exists(timingsafe_memcmp HAVE_TIMINGSAFE_MEMCMP)
if(HAVE_MEMCMP)
	add_definitions(-DHAVE_MEMCMP)
endif()

check_function_exists(memmem HAVE_MEMMEM)
if(HAVE_MEMMEM)
	add_definitions(-DHAVE_MEMMEM)
endif()

check_include_files(err.h HAVE_ERR_H)
if(HAVE_ERR_H)
	add_definitions(-DHAVE_ERR_H)
endif()

check_function_exists(usleep HAVE_USLEEP)
if(HAVE_USLEEP)
	add_definitions(-DHAVE_USLEEP)
endif()

check_function_exists(getopt HAVE_GETOPT)
if(HAVE_GETOPT)
        add_definitions(-DHAVE_GETOPT)
endif()

if(NOT HAVE_FREEZERO)
	list(APPEND SRCS ${IKED_COMPAT}/freezero.c)
endif()
if(NOT HAVE_RECALLOCARRAY)
	list(APPEND SRCS ${IKED_COMPAT}/recallocarray.c)
endif()
if(NOT HAVE_SOCK_NONBLOCK)
	list(APPEND SRCS ${IKED_COMPAT}/bsd-socket.c)
endif()
if(NOT HAVE_SETRESUID)
	list(APPEND SRCS ${IKED_COMPAT}/bsd-setres_id.c)
endif()
if(NOT HAVE_GETRTABLE)
	list(APPEND SRCS ${IKED_COMPAT}/getrtable.c)
endif()
if(NOT HAVE_GETDTABLECOUNT)
	list(APPEND SRCS ${IKED_COMPAT}/getdtablecount.c)
endif()
if(NOT HAVE_SETPROCTITLE)
	list(APPEND SRCS ${IKED_COMPAT}/setproctitle.c)
endif()
if(NOT HAVE_STRTONUM)
	list(APPEND SRCS ${IKED_COMPAT}/strtonum.c)
endif()
if(NOT HAVE_FFS)
	list(APPEND SRCS ${IKED_COMPAT}/ffs.c)
endif()
if(NOT HAVE_GETOPT)
	list(APPEND SRCS ${IKED_COMPAT}/getopt_long.c)
endif()
if(NOT HAVE_IMSG_H)
	list(APPEND SRCS
		# imsg
		${IKED_COMPAT}/imsg.c
		${IKED_COMPAT}/imsg-buffer.c
	)
endif()
if(NOT HAVE_STRLCPY)
	list(APPEND SRCS ${IKED_COMPAT}/strlcpy.c)
endif()
if(NOT HAVE_STRLCAT)
	list(APPEND SRCS ${IKED_COMPAT}/strlcat.c)
endif()

list(APPEND SRCS
	ikeca.c
	ikectl.c
	parser.c
	# iked
	${CMAKE_CURRENT_SOURCE_DIR}/../iked/log.c
	${CMAKE_CURRENT_SOURCE_DIR}/../iked/util.c
)

add_executable(ikectl ${SRCS})

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
	-Werror
	"$<$<CONFIG:DEBUG>:-O0;-g>"
)

target_compile_options(ikectl PRIVATE ${CFLAGS})

target_include_directories(ikectl
	PRIVATE ${IKED_COMPAT}
		${CMAKE_CURRENT_SOURCE_DIR}
		${CMAKE_CURRENT_SOURCE_DIR}/../iked
)

target_link_libraries(ikectl
	PRIVATE util event crypto ssl
)
