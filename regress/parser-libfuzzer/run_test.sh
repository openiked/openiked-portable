#!/bin/sh

# script to run the parser-fuzzer with the right options

# ASAN-option to help finding the source of memory leaks
export ASAN_OPTIONS=fast_unwind_on_malloc=0

$(dirname "$0")/test_libfuzzer -dict=$(dirname "$0")/fuzz.dict -max_len=8164 -max_total_time=60 -error_exitcode=0 $(dirname "$0")/corpus

#TODO: remove error_exitcode=0 for github CI
#maybe the corpus shold be on a persistent storage 
#total_time is maybe a little bit short
