#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

static int is_alnum(char c) {
  return (c >= '0' && c <= '9') ||
         (c >= 'A' && c <= 'Z') ||
         (c >= 'a' && c <= 'z');
}

int
main(int argc, char *argv[])
{
  // Grab current break
  char *base = (char*)sbrk(0);
  if (base == (char*)-1)
    exit(1);

  // Allocate a decent chunk of pages to catch freed ones from 'secret'.
  // 256 KB is plenty while staying safe on memory.
  int bytes = 64 * 4096; // 64 pages
  if ((long)sbrk(bytes) == -1)
    exit(1);

  char *start = base;
  char *end   = base + bytes;

  char *best = 0;
  int bestlen = 0;

  char *run = 0;
  int runlen = 0;

  for (char *p = start; p < end; p++) {
    if (is_alnum(*p)) {
      if (runlen == 0) run = p;
      runlen++;
    } else {
      if (runlen > bestlen) {
        best = run;
        bestlen = runlen;
      }
      runlen = 0;
    }
  }
  // Tail run
  if (runlen > bestlen) {
    best = run;
    bestlen = runlen;
  }

  if (best && bestlen > 0) {
    // Print exactly the secret line (letters/digits only), once.
    write(1, best, bestlen);
    write(1, "\n", 1);
  }

  exit(0);
}

