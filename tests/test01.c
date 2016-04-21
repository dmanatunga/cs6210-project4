/*
 * Test that rvm_destroy() returns immediately and the backing file still exists
 */
#include "rvm.h"
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>

#define TEST_STRING "hello, world"
#define OFFSET2 1000

int file_exists(const char* name) {
  struct stat buffer;
  return stat(name, &buffer) == 0;
}

int main(int argc, char** argv) {
  rvm_t rvm;
  trans_t trans;
  char* segs[1];

  rvm = rvm_init("rvm_segments");
  rvm_destroy(rvm, "testseg01");
  segs[0] = (char*) rvm_map(rvm, "testseg01", 10000);

  trans = rvm_begin_trans(rvm, 1, (void**) segs);

  rvm_about_to_modify(trans, segs[0], 0, 100);
  sprintf(segs[0], TEST_STRING);

  rvm_about_to_modify(trans, segs[0], OFFSET2, 100);
  sprintf(segs[0] + OFFSET2, TEST_STRING);

  rvm_commit_trans(trans);

  // This should fail
  rvm_destroy(rvm, "testseg01");

  rvm_unmap(rvm, segs[0]);

  rvm_truncate_log(rvm);

  if (file_exists("rvm_segments/seg_testseg01.rvm")) {
    printf("OK\n");
  } else {
    printf("ERROR: Segment file no longer present");
  }

  return 0;
}
