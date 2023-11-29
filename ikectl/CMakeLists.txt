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

set(SRCS)
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

target_compile_options(ikectl PRIVATE ${CFLAGS})
if(HAVE_LD_Z)
	target_link_options(ikectl PRIVATE "LINKER:-z,relro,-z,now")
endif()

target_include_directories(ikectl
	PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
		${CMAKE_CURRENT_SOURCE_DIR}/../iked
)

target_link_libraries(ikectl
	PRIVATE util event crypto ssl compat
)

install(TARGETS ikectl RUNTIME DESTINATION ${CMAKE_INSTALL_SBINDIR})
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/ikectl.8 DESTINATION ${CMAKE_INSTALL_MANDIR}/man8/)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/ikeca.cnf DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/ssl)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/ikex509v3.cnf DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/ssl)
