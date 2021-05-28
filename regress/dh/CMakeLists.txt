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
	dhtest.c
	${CMAKE_CURRENT_SOURCE_DIR}/../../iked/crypto_hash.c
	${CMAKE_CURRENT_SOURCE_DIR}/../../iked/dh.c
	${CMAKE_CURRENT_SOURCE_DIR}/../../iked/smult_curve25519_ref.c
	${CMAKE_CURRENT_SOURCE_DIR}/../../iked/sntrup761.c
	${CMAKE_CURRENT_SOURCE_DIR}/../../iked/imsg_util.c
)

add_executable(dhtest ${SRCS})

target_include_directories(dhtest
	PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../../iked
)

target_link_libraries(dhtest
	PRIVATE util crypto compat
)

target_compile_options(dhtest PRIVATE ${CFLAGS})
