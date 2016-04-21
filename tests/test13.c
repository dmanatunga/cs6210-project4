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
  char* segs[1];

  rvm = rvm_init("rvm_segments");
  rvm_destroy(rvm, "testseg");
  segs[0] = (char*) rvm_map(rvm, "testseg", 10000);

  trans = rvm_begin_trans(rvm, 1, (void**) segs);

  rvm_about_to_modify(trans, segs[0], 0, 100);
  sprintf(segs[0], TEST_STRING);

  rvm_about_to_modify(trans, segs[0], OFFSET2, 100);
  sprintf(segs[0] + OFFSET2, TEST_STRING);

  rvm_abort_trans(100);

  fprintf(stderr, "ERROR: Wrong abort transaction id\n");

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
