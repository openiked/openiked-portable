# Copyright (c) 2022-2023 David Linder
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
	common.c
	test_parser_fuzz.c
)

set(CMAKE_C_COMPILER clang)
add_executable(test_libfuzzer ${SRCS})

target_include_directories(test_libfuzzer
	PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../../iked
)

# when using clusterfuzz we need static linking and clusterfuzz' compiler arguments
if (DEFINED ENV{CLUSTERFUZZLITE})
	target_link_libraries(test_libfuzzer
		"-lm -Wl,-Bstatic -lssl -lcrypto -levent" compat iked-shared $ENV{LIB_FUZZING_ENGINE}
	)
else()
	string(APPEND CMAKE_C_FLAGS " -fsanitize=fuzzer")
	string(APPEND CMAKE_EXE_LINKER_FLAGS " -fsanitize=fuzzer")
	target_link_libraries(test_libfuzzer
		PRIVATE event crypto ssl compat iked-shared
	)
	file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/corpus)
	configure_file(run_test.sh run_test.sh COPYONLY)
	configure_file(test_libfuzzer.dict fuzz.dict COPYONLY)
endif()

target_compile_options(test_libfuzzer PRIVATE ${CFLAGS})

