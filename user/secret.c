#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  if(argc < 2)
    exit(1);

  // Allocate one page and leave the secret in it.
  char *base = (char*) sbrk(4096);
  if(base == (char*)-1)
    exit(1);

  char *where = base + 32;   // stash offset inside the page
  strcpy(where, argv[1]);    // leave the secret bytes behind
  exit(0);
}
