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

set(CMAKE_C_COMPILER clang)

# address-sanitizer not working on OpenBSD
set(CMAKE_C_FLAGS "-g -O1 -fsanitize=fuzzer,address")
set(CMAKE_EXE_LINKER_FLAGS "-fsanitize=fuzzer,address")

set(SRCS)
list(APPEND SRCS
	common.c
	test_parser_fuzz.c
	$<TARGET_OBJECTS:iked-shared>
)

add_executable(test_libfuzzer ${SRCS})

target_include_directories(test_libfuzzer
	PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../../iked
)

target_link_libraries(test_libfuzzer
	PRIVATE util event crypto ssl compat
)

target_compile_options(test_libfuzzer PRIVATE ${CFLAGS})

file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/corpus)
configure_file(run_test.sh run_test.sh COPYONLY)
configure_file(fuzz.dict fuzz.dict COPYONLY)
