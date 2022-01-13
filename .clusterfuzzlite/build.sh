#!/bin/bash -eu

# build project
mkdir build
cd build/
cmake -DCMAKE_BUILD_TYPE=Release -DCLUSTERFUZZ=ON ..
make

# copy binary and dict to $OUT
cp regress/parser-libfuzzer/test_libfuzzer $OUT/
cp ../regress/parser-libfuzzer/test_libfuzzer.dict ../regress/parser-libfuzzer/test_libfuzzer.options $OUT/
