#!/bin/bash -eu

# build project
cmake -S . -B build -DCMAKE_BUILD_TYPE=DEBUG -DCLUSTERFUZZ=ON
cmake --build build

# copy binary and dict to $OUT
cp build/regress/parser-libfuzzer/test_libfuzzer $OUT/
cp regress/parser-libfuzzer/test_libfuzzer.dict regress/parser-libfuzzer/test_libfuzzer.options $OUT/
