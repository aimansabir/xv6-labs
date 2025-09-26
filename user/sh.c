// Improved xv6 shell: adds history, wait, background jobs, and suppresses prompt if not interactive.

#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fcntl.h"
int strncmp(const char *p, const char *q, uint n);

#define MAXARGS 16
#define MAXLINE 128
#define HISTORY_SIZE 32

// ------------ History ------------
static char history[HISTORY_SIZE][MAXLINE];
static int history_head = 0;
static int history_count = 0;

static void
add_history(const char *line)
{
  int len;
  if(!line || !*line) return;
  len = strlen(line);
  if(len > 0 && line[len-1] == '\n') len--;
  if(len == 0) return;
  if(len >= MAXLINE) len = MAXLINE-1;
  memmove(history[history_head], line, len);
  history[history_head][len] = 0;
  history_head = (history_head + 1) % HISTORY_SIZE;
  history_count++;
}

static void
show_history(void)
{
  int total = history_count < HISTORY_SIZE ? history_count : HISTORY_SIZE;
  int start = (history_head - total + HISTORY_SIZE) % HISTORY_SIZE;
  for(int i = 0; i < total; i++){
    int idx = (start + i) % HISTORY_SIZE;
    printf("%d  %s\n", i+1, history[idx]);
  }
}

// ------------ Detect interactive (prompt only if tty-ish) ------------
static int
is_interactive(void)
{
  struct stat st;
  if(fstat(0, &st) < 0) return 1;
  if(st.size == 0) return 1; // console
  return 0;
}

// ------------ Command structs (original xv6) ------------
struct cmd {
  int type;
};

struct execcmd {
  int type;
  char *argv[MAXARGS];
  char *eargv[MAXARGS];
};

struct redircmd {
  int type;
  struct cmd *cmd;
  char *file;
  char *efile;
  int mode;
  int fd;
};

struct pipecmd {
  int type;
  struct cmd *left;
  struct cmd *right;
};

struct listcmd {
  int type;
  struct cmd *left;
  struct cmd *right;
};

struct backcmd {
  int type;
  struct cmd *cmd;
};

enum { EXEC = 1, REDIR, PIPE, LIST, BACK };

// ------------ Decls ------------
int fork1(void);
void panic(char*);
struct cmd *parsecmd(char*);

// ------------ runcmd (original xv6 with back jobs) ------------
void
runcmd(struct cmd *cmd)
{
  int p[2];
  struct backcmd *bcmd;
  struct execcmd *ecmd;
  struct listcmd *lcmd;
  struct pipecmd *pcmd;
  struct redircmd *rcmd;

  if(cmd == 0)
    exit(1);

  switch(cmd->type){
  default:
    panic("runcmd");

  case EXEC:
    ecmd = (struct execcmd*)cmd;
    if(ecmd->argv[0] == 0)
      exit(1);
    exec(ecmd->argv[0], ecmd->argv);
    fprintf(2, "exec %s failed\n", ecmd->argv[0]);
    break;

  case REDIR:
    rcmd = (struct redircmd*)cmd;
    close(rcmd->fd);
    if(open(rcmd->file, rcmd->mode) < 0){
      fprintf(2, "open %s failed\n", rcmd->file);
      exit(1);
    }
    runcmd(rcmd->cmd);
    break;

  case LIST:
    lcmd = (struct listcmd*)cmd;
    if(fork1() == 0)
      runcmd(lcmd->left);
    wait(0);
    runcmd(lcmd->right);
    break;

  case PIPE:
    pcmd = (struct pipecmd*)cmd;
    if(pipe(p) < 0)
      panic("pipe");
    if(fork1() == 0){
      close(1);
      dup(p[1]);
      close(p[0]);
      close(p[1]);
      runcmd(pcmd->left);
    }
    if(fork1() == 0){
      close(0);
      dup(p[0]);
      close(p[0]);
      close(p[1]);
      runcmd(pcmd->right);
    }
    close(p[0]);
    close(p[1]);
    wait(0);
    wait(0);
    break;

  case BACK:
    bcmd = (struct backcmd*)cmd;
    if(fork1() == 0)
      runcmd(bcmd->cmd);
    break;
  }
  exit(0);
}

// ------------ getcmd ------------
int
getcmd(char *buf, int nbuf)
{
  if(is_interactive()){
    write(2, "$ ", 2);
  }

  memset(buf, 0, nbuf);
  if(gets(buf, nbuf) == 0){
    if(is_interactive()) write(2, "\n", 1);
    return -1;
  }

  if(strchr(buf, '\t')){
    printf("[tab detected]\n");
    char *p = buf, *q = buf;
    while(*p){
      if(*p != '\t') *q++ = *p;
      p++;
    }
    *q = 0;
  }

  add_history(buf);
  return 0;
}

// ------------ main loop ------------
int
main(void)
{
  static char buf[MAXLINE];
  int fd;

  while((fd = open("console", O_RDWR)) >= 0){
    if(fd >= 3){
      close(fd);
      break;
    }
  }

  for(;;){
    if(getcmd(buf, sizeof(buf)) < 0)
      break;

    char *cmd = buf;
    while(*cmd == ' ' || *cmd == '\t') cmd++;
    if(*cmd == 0 || *cmd == '\n') continue;

    if(strncmp(cmd, "cd ", 3) == 0){
      cmd[strlen(cmd)-1] = 0;
      if(chdir(cmd+3) < 0)
        fprintf(2, "cannot cd %s\n", cmd+3);
      continue;
    }

    if(strcmp(cmd, "exit\n") == 0 || strcmp(cmd, "exit") == 0){
      exit(0);
    }

    if(strcmp(cmd, "history\n") == 0 || strcmp(cmd, "history") == 0){
      show_history();
      continue;
    }

    if(strncmp(cmd, "wait", 4) == 0 &&
       (cmd[4] == 0 || cmd[4] == '\n' || cmd[4] == ' ')){
      while(wait(0) > 0) { }
      continue;
    }

    int len = strlen(cmd);
    if(len > 0 && cmd[len-1] == '\n') cmd[len-1] = 0;
    len = strlen(cmd);

    int bg = 0;
    if(len > 0 && cmd[len-1] == '&'){
      bg = 1;
      cmd[len-1] = 0;
    }

    int pid = fork1();
    if(pid == 0){
      runcmd(parsecmd(cmd));
      exit(0);
    } else {
      if(!bg){
        wait(0);
      } else {
        printf("[bg %d]\n", pid);
      }
    }
  }
  exit(0);
}

// ------------ helpers ------------
void
panic(char *s)
{
  fprintf(2, "%s\n", s);
  exit(1);
}

int
fork1(void)
{
  int pid = fork();
  if(pid == -1)
    panic("fork");
  return pid;
}

// ------------ parser (unchanged xv6) ------------
char whitespace[] = " \t\r\n\v";
char symbols[] = "<|>&;()";

int gettoken(char **ps, char *es, char **q, char **eq);
int peek(char **ps, char *es, char *toks);
struct cmd *parseline(char **ps, char *es);
struct cmd *parsepipe(char **ps, char *es);
struct cmd *parseexec(char **ps, char *es);
struct cmd *nulterminate(struct cmd *cmd);

struct cmd*
parsecmd(char *s)
{
  char *es;
  struct cmd *cmd;

  es = s + strlen(s);
  cmd = parseline(&s, es);
  if(s != es){
    fprintf(2, "leftovers: %s\n", s);
    panic("syntax");
  }
  nulterminate(cmd);
  return cmd;
}

struct cmd*
parseline(char **ps, char *es)
{
  struct cmd *cmd;

  cmd = parsepipe(ps, es);
  while(peek(ps, es, "&")){
    gettoken(ps, es, 0, 0);
    cmd = (struct cmd*)malloc(sizeof(struct backcmd));
    ((struct backcmd*)cmd)->type = BACK;
    ((struct backcmd*)cmd)->cmd = cmd;
  }
  if(peek(ps, es, ";")){
    gettoken(ps, es, 0, 0);
    cmd = (struct cmd*)malloc(sizeof(struct listcmd));
    ((struct listcmd*)cmd)->type = LIST;
    ((struct listcmd*)cmd)->left = cmd;
    ((struct listcmd*)cmd)->right = parseline(ps, es);
  }
  return cmd;
}

struct cmd*
parsepipe(char **ps, char *es)
{
  struct cmd *cmd;

  cmd = parseexec(ps, es);
  if(peek(ps, es, "|")){
    gettoken(ps, es, 0, 0);
    struct pipecmd *p = (struct pipecmd*)malloc(sizeof(*p));
    memset(p, 0, sizeof(*p));
    p->type = PIPE;
    p->left = cmd;
    p->right = parsepipe(ps, es);
    cmd = (struct cmd*)p;
  }
  return cmd;
}

struct cmd*
parseredirs(struct cmd *cmd, char **ps, char *es)
{
  int tok;
  char *q, *eq;

  while(peek(ps, es, "<>")){
    tok = gettoken(ps, es, 0, 0);
    if(gettoken(ps, es, &q, &eq) != 'a')
      panic("missing file for redirection");
    switch(tok){
    case '<':
      {
        struct redircmd *rc = (struct redircmd*)malloc(sizeof(*rc));
        rc->type = REDIR;
        rc->cmd = cmd;
        rc->file = q;
        rc->efile = eq;
        rc->mode = O_RDONLY;
        rc->fd = 0;
        cmd = (struct cmd*)rc;
      }
      break;
    case '>':
      {
        struct redircmd *rc = (struct redircmd*)malloc(sizeof(*rc));
        rc->type = REDIR;
        rc->cmd = cmd;
        rc->file = q;
        rc->efile = eq;
        rc->mode = O_WRONLY|O_CREATE|O_TRUNC;
        rc->fd = 1;
        cmd = (struct cmd*)rc;
      }
      break;
    case '+':
      {
        struct redircmd *rc = (struct redircmd*)malloc(sizeof(*rc));
        rc->type = REDIR;
        rc->cmd = cmd;
        rc->file = q;
        rc->efile = eq;
        rc->mode = O_WRONLY|O_CREATE;
        rc->fd = 1;
        cmd = (struct cmd*)rc;
      }
      break;
    }
  }
  return cmd;
}

struct cmd*
parseexec(char **ps, char *es)
{
  char *q, *eq;
  int tok, argc;
  struct execcmd *cmd;
  struct cmd *ret;

  if(peek(ps, es, "(")){
    gettoken(ps, es, 0, 0);
    ret = parseline(ps, es);
    if(!peek(ps, es, ")"))
      panic("syntax - missing )");
    gettoken(ps, es, 0, 0);
    ret = parseredirs(ret, ps, es);
    return ret;
  }

  ret = (struct cmd*)malloc(sizeof(*cmd));
  cmd = (struct execcmd*)ret;
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = EXEC;

  argc = 0;
  ret = parseredirs(ret, ps, es);
  while(!peek(ps, es, "|)&;")){
    if((tok = gettoken(ps, es, &q, &eq)) == 0)
      break;
    if(tok != 'a')
      panic("syntax");
    cmd->argv[argc] = q;
    cmd->eargv[argc] = eq;
    argc++;
    if(argc >= MAXARGS)
      panic("too many args");
    ret = parseredirs(ret, ps, es);
  }
  cmd->argv[argc] = 0;
  cmd->eargv[argc] = 0;
  return ret;
}

struct cmd*
nulterminate(struct cmd *cmd)
{
  int i;
  struct execcmd *ecmd;
  struct redircmd *rcmd;
  struct pipecmd *pcmd;
  struct listcmd *lcmd;
  struct backcmd *bcmd;

  if(cmd == 0)
    return 0;

  switch(cmd->type){
  case EXEC:
    ecmd = (struct execcmd*)cmd;
    for(i=0; ecmd->argv[i]; i++)
      *ecmd->eargv[i] = 0;
    break;
  case REDIR:
    rcmd = (struct redircmd*)cmd;
    nulterminate(rcmd->cmd);
    *rcmd->efile = 0;
    break;
  case PIPE:
    pcmd = (struct pipecmd*)cmd;
    nulterminate(pcmd->left);
    nulterminate(pcmd->right);
    break;
  case LIST:
    lcmd = (struct listcmd*)cmd;
    nulterminate(lcmd->left);
    nulterminate(lcmd->right);
    break;
  case BACK:
    bcmd = (struct backcmd*)cmd;
    nulterminate(bcmd->cmd);
    break;
  }
  return cmd;
}

// ------------ lexer helpers ------------
int
gettoken(char **ps, char *es, char **q, char **eq)
{
  char *s;
  int ret;

  s = *ps;
  while(s < es && strchr(whitespace, *s))
    s++;
  if(q) *q = s;
  ret = *s;
  switch(*s){
  case 0:
    break;
  case '|':
  case '(':
  case ')':
  case ';':
  case '&':
  case '<':
    s++;
    break;
  case '>':
    s++;
    if(*s == '>'){
      ret = '+';
      s++;
    }
    break;
  default:
    ret = 'a';
    while(s < es && !strchr(whitespace, *s) && !strchr(symbols, *s))
      s++;
    break;
  }
  if(eq) *eq = s;

  while(s < es && strchr(whitespace, *s))
    s++;
  *ps = s;
  return ret;
}

int
peek(char **ps, char *es, char *toks)
{
  char *s;

  s = *ps;
  while(s < es && strchr(whitespace, *s))
    s++;
  *ps = s;
  return *s && strchr(toks, *s);
}

