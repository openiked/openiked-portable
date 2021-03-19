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
	tests.c
	common.c
	test_parser_fuzz.c
)

add_executable(test_parser ${SRCS})

target_include_directories(test_parser
	PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../test_helper
	PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../../iked
)

target_link_libraries(test_parser
	PRIVATE util event crypto ssl test_helper compat iked-shared
)

target_compile_options(test_parser PRIVATE ${CFLAGS})
