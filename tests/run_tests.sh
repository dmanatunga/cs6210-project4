#!/bin/bash

make
LD_LIBRARY_PATH=../ ./basic
LD_LIBRARY_PATH=../ ./abort
LD_LIBRARY_PATH=../ ./multi-abort
LD_LIBRARY_PATH=../ ./multi
LD_LIBRARY_PATH=../ ./truncate

for i in `seq 24`; do
  printf -v i "%02d" $i
  printf -v bench test${i}
  echo "Running $bench"
  LD_LIBRARY_PATH=../ ./$bench
  rm -rf rvm_segments linked_list
done
