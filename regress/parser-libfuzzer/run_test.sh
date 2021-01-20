#!/bin/sh

# script to run the parser-fuzzer with the right options

# ASAN-option to help finding the source of memory leaks
export ASAN_OPTIONS=fast_unwind_on_malloc=0

$(dirname "$0")/parse_test -dict=$(dirname "$0")/fuzz.dict -max_len=15000 -max_total_time=60 $(dirname "$0")/corpus

#maybe the corpus shold be on a persistent storage 
#total_time is maybe a little bit short
