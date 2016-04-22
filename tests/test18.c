/*
 * Test that rvm_destroy() returns immediately and the backing file still exists
 */
#include "rvm.h"
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_STRING "hello, world"
#define OFFSET2 1000
#define OFFSET3 700
#define OFFSET4 300

int main(int argc, char** argv) {
  rvm_t rvm;
  char* segs[1];

  system("mkdir -p rvm_segments");
  system("cp corrupt_redo_log2.rvm rvm_segments/redo_log.rvm");

  rvm = rvm_init("rvm_segments");

  segs[0] = (char*) rvm_map(rvm, "testseg", 10000);
  if (strcmp(segs[0], TEST_STRING)) {
    printf("ERROR: first hello not present\n");
    exit(2);
  }

  if (strcmp(segs[0] + OFFSET2, TEST_STRING)) {
    printf("ERROR: second hello not present\n");
    exit(2);
  }

  for (int i = 0; i < 100; i++) {
    if (segs[0][OFFSET3 + i] != 0) {
      printf("ERROR: OFFSET3 Region should be 0 not %d\n", segs[0][OFFSET3 + i]);
      exit(2);
    }

    if (segs[0][OFFSET4 + i] != 0) {
      printf("ERROR: OFFSET4 Region should be 0 not %d\n", segs[0][OFFSET4 + i]);
      exit(2);
    }
  }


  printf("OK\n");


  return 0;
}
