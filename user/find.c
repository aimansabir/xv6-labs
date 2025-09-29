// user/find.c
#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fs.h"
#include "kernel/param.h"   // MAXARG
#include "user/user.h"

static int isdot(const char *name) {
  return (strcmp(name, ".") == 0 || strcmp(name, "..") == 0);
}

static void run_exec(char **cmdv, int cmdc, char *filepath) {
  // Build argv: cmdv[0..cmdc-1], then filepath, then 0
  if (cmdc + 1 >= MAXARG) {
    fprintf(2, "find: too many args for -exec\n");
    return;
  }
  char *argv[MAXARG];
  for (int i = 0; i < cmdc; i++) argv[i] = cmdv[i];
  argv[cmdc] = filepath;
  argv[cmdc + 1] = 0;

  int pid = fork();
  if (pid == 0) {
    exec(argv[0], argv);
    // only if exec fails:
    fprintf(2, "exec %s failed\n", argv[0]);
    exit(1);
  } else if (pid > 0) {
    wait(0);
  } else {
    fprintf(2, "find: fork failed\n");
  }
}

static void do_find(char *path, char *target, int do_exec, char **cmdv, int cmdc) {
  int fd;
  struct stat st;

  if ((fd = open(path, 0)) < 0) {
    fprintf(2, "find: cannot open %s\n", path);
    return;
  }
  if (fstat(fd, &st) < 0) {
    fprintf(2, "find: cannot stat %s\n", path);
    close(fd);
    return;
  }

  if (st.type == T_FILE) {
    // compare basename(path) against target
    char *base = path + strlen(path);
    while (base > path && *(base - 1) != '/') base--;
    if (strcmp(base, target) == 0) {
      if (do_exec) run_exec(cmdv, cmdc, path);
      else printf("%s\n", path);
    }
  } else if (st.type == T_DIR) {
    char buf[512], *p;
    struct dirent de;

    if (strlen(path) + 1 + DIRSIZ + 1 > sizeof(buf)) {
      fprintf(2, "find: path too long\n");
      close(fd);
      return;
    }

    strcpy(buf, path);
    p = buf + strlen(buf);
    *p++ = '/';

    while (read(fd, &de, sizeof(de)) == sizeof(de)) {
      if (de.inum == 0) continue;
      if (isdot(de.name)) continue;

      memmove(p, de.name, DIRSIZ);
      p[DIRSIZ] = 0;

      // recurse
      do_find(buf, target, do_exec, cmdv, cmdc);
    }
  }

  close(fd);
}

int main(int argc, char *argv[]) {
  if (argc < 3) {
    fprintf(2, "Usage:\n  find <path> <name> [-exec <cmd> [args...]]\n");
    exit(1);
  }

  char *path   = argv[1];
  char *target = argv[2];

  int do_exec = 0;
  char **cmdv = 0;
  int cmdc = 0;

  if (argc >= 4 && strcmp(argv[3], "-exec") == 0) {
    do_exec = 1;
    cmdv = &argv[4];
    cmdc = argc - 4;
    if (cmdc <= 0) {
      fprintf(2, "find: -exec needs a command\n");
      exit(1);
    }
    if (cmdc >= MAXARG-1) {
      fprintf(2, "find: -exec too many args\n");
      exit(1);
    }
  }

  do_find(path, target, do_exec, cmdv, cmdc);
  exit(0);
}

