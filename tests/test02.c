#include "rvm.h"
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

/* proc1 writes some data, commits it, then exits */
void proc1() {
  rvm_t rvm;
  char* segs[1];

  rvm = rvm_init("rvm_segments");
  rvm_destroy(rvm, "testseg01");
  segs[0] = (char*) rvm_map(rvm, "testseg01", 10000);

  // this should fail
  if(rvm_map(rvm, "testseg01", 10000) != -1)
    fprintf(stderr, "Should return -1\n");
  else
    fprintf(stderr, "Pass\n");

  abort();
}

void proc2() {
  rvm_t rvm;
  char* segs[1];

  rvm = rvm_init("rvm_segments");
  rvm_destroy(rvm, "testseg01");
  segs[0] = (char*) rvm_map(rvm, "testseg01", 10000);

  rvm_unmap(rvm, segs[0]);

  // this should not fail
  rvm_destroy(rvm, "testseg01");

  return;
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
