#!/bin/bash

make

for i in `seq 24`; do
  printf -v i "%02d" $i
  printf -v bench test${i}
  echo "Running $bench"
  LD_LIBRARY_PATH=../ ./$bench
  rm -rf rvm_segments linked_list
done
