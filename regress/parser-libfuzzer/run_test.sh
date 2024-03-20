#!/bin/sh

# script to run the parser-fuzzer for 5 minutes with the right options
# use repo github.com/openiked/openiked-fuzzing/corpus/test_libfuzzer as corpus for faster results

# ASAN-option to help finding the source of memory leaks
export ASAN_OPTIONS=fast_unwind_on_malloc=0

$(dirname "$0")/test_libfuzzer -dict=$(dirname "$0")/fuzz.dict -max_len=8164 -max_total_time=300 $(dirname "$0")/corpus
