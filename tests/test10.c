/* 
 * Test that rvm_about_to_modify() with a negative size fails
 */

#include "rvm.h"
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

#define TEST_STRING "hello, world"
#define OFFSET2 1000

void proc1() {
  rvm_t rvm;
  trans_t trans = 0;
  char *segs1[2];

  rvm = rvm_init("rvm_segments");
  rvm_destroy(rvm, "testseg");
  segs1[0] = (char*) rvm_map(rvm, "testseg", 10000);

  trans = rvm_begin_trans(rvm, 1, (void**) segs1);

  // this should exit
  rvm_about_to_modify(trans, segs1[0], 1000000, -1);

  fprintf(stderr, "ERROR: Modifying segment with negative size\n");

  abort();
}

int main(int argc, char** argv) {
  int pid;

  pid = fork();
  if (pid < 0) {
    perror("fork");
    exit(2);
  }
  if (pid == 0) {
    proc1();
    exit(0);
  }

  waitpid(pid, NULL, 0);

  return 0;
}
