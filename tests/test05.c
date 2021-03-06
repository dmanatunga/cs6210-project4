/* 
 * Test that rvm_begin_trans() with invalid segment fails
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
  trans_t trans;
  char* segs[1] = { 0 };

  rvm = rvm_init("rvm_segments");
  rvm_destroy(rvm, "testseg");

  // Invalid segment
  trans = rvm_begin_trans(rvm, 1, (void**) segs);

  if (trans != -1) {
    fprintf(stderr, "Error: Should be -1\n");
  }

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
