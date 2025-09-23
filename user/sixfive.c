#include "kernel/types.h"
#include "user/user.h"
#include "kernel/fcntl.h"

static char *separators = " -\r\t\n./,";

static int
is_sep(char c)
{
  char *p;
  for (p = separators; *p; p++) {
    if (*p == c) return 1;
  }
  return 0;
}

static void
process_fd(int fd)
{
  char buf[64];
  int idx = 0;
  char c;
  int n;

  while (read(fd, &c, 1) == 1) {
    if (c >= '0' && c <= '9') {
      if (idx < sizeof(buf) - 1) {
        buf[idx++] = c;
      }
    } else if (is_sep(c)) {
      if (idx > 0) {
        buf[idx] = '\0';
        n = atoi(buf);
        if (n % 5 == 0 || n % 6 == 0) {
          printf("%d\n", n);
        }
        idx = 0;
      }
    }
  }

  // handle number at EOF
  if (idx > 0) {
    buf[idx] = '\0';
    n = atoi(buf);
    if (n % 5 == 0 || n % 6 == 0) {
      printf("%d\n", n);
    }
  }
}

int
main(int argc, char *argv[])
{
  if (argc < 2) {
    fprintf(2, "Usage: sixfive files...\n");
    exit(1);
  }

  for (int i = 1; i < argc; i++) {
    int fd = open(argv[i], O_RDONLY);
    if (fd < 0) {
      fprintf(2, "sixfive: cannot open %s\n", argv[i]);
      continue;
    }
    process_fd(fd);
    close(fd);
  }

  exit(0);
}
