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

set(IKED_COMPAT		"${CMAKE_CURRENT_SOURCE_DIR}")
set(SRCS)

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
if(NOT HAVE_IMSG_H OR NOT HAVE_MSGBUF_NEW_READER)
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
if(NOT HAVE_FREEZERO)
	list(APPEND SRCS ${IKED_COMPAT}/freezero.c)
endif()
if(NOT HAVE_ARC4RANDOM_BUF)
	list(APPEND SRCS ${IKED_COMPAT}/arc4random.c)
endif()
if(NOT HAVE_ARC4RANDOM_UNIFORM)
	list(APPEND SRCS ${IKED_COMPAT}/arc4random_uniform.c)
endif()
if(NOT HAVE_EXPLICIT_BZERO)
	list(APPEND SRCS ${IKED_COMPAT}/explicit_bzero.c)
endif()
if(NOT HAVE_REALLOCARRAY)
	list(APPEND SRCS ${IKED_COMPAT}/reallocarray.c)
endif()
if(NOT HAVE_VIS)
	list(APPEND SRCS ${IKED_COMPAT}/vis.c)
endif()

set(CFLAGS)
list(APPEND CFLAGS
	-O2
	-fstack-protector-strong
	-fPIE
	-D_FORTIFY_SOURCE=2
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

if(SRCS)
	add_library(compat OBJECT ${SRCS})
	target_compile_options(compat PRIVATE ${CFLAGS})
	target_include_directories(compat PUBLIC .)
else()
	add_library(compat INTERFACE)
	target_include_directories(compat INTERFACE .)
endif()
