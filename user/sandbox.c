#include "kernel/types.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  if (argc < 4) {
    // Usage: sandbox <mask> <path|- > <cmd> [args...]
    fprintf(2, "usage: sandbox <mask> <path> <cmd> [args...]\n");
    exit(1);
  }

  int mask = atoi(argv[1]);
  char *path = argv[2];

  int pid = fork();
  if (pid < 0) {
    fprintf(2, "sandbox: fork failed\n");
    exit(1);
  }
  if (pid == 0) {
    // child: apply restrictions then exec the command
    if (interpose(mask, path) < 0) {
      fprintf(2, "sandbox: interpose failed\n");
      exit(1);
    }
    exec(argv[3], &argv[3]);
    fprintf(2, "sandbox: exec %s failed\n", argv[3]);
    exit(1);
  }
  // parent: wait
  wait(0);
  exit(0);
}
