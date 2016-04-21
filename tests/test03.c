/* 
 * Test that rvm_unmap() with invalid segment base fails
 */

#include "rvm.h"
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

void proc1() {
  rvm_t rvm;
  char* segs[1] = { 0 };

  rvm = rvm_init("rvm_segments");

  // this should fail
  rvm_unmap(rvm, segs[0]);

  printf("ERROR: Unmap() before mapping\n");
  segs[0] = (char*) rvm_map(rvm, "testseg03", 10000);

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

  return 0;
}
