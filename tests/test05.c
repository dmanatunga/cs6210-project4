/* basic.c - test that basic persistency works */

#include "rvm.h"
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

#define TEST_STRING "hello, world"
#define OFFSET2 1000


/* proc1 writes some data, commits it, then exits */
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
  } else {
    fprintf(stderr, "Pass\n");
  }

  segs[0] = (char*) rvm_map(rvm, "testseg", 10000);
  abort();
}


/* proc2 opens the segments and reads from them */
void proc2() {
  exit(0);
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

  proc2();

  return 0;
}
