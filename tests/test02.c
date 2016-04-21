#include "rvm.h"
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

/* proc1 writes some data, commits it, then exits */
void proc1() {
  rvm_t rvm;
  char* segs[1] = { 0 };

  rvm = rvm_init("rvm_segments");
  rvm_destroy(rvm, "testseg02");
  segs[0] = (char*) rvm_map(rvm, "testseg02", 10000);
  segs[0] = (char*) rvm_map(rvm, "testseg02", 10000);

  // this should fail
  if(segs[0] != (char *)-1)
    fprintf(stderr, "ERROR: Calling map twice Should return -1\n");
  else
    fprintf(stderr, "OK\n");

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
