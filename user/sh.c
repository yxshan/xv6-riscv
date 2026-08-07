// xv6 的 shell。
//
// 它的工作方式与 Unix 传统 shell 相同：
// 把命令行解析成一棵命令树（普通命令、重定向、管道、顺序列表、后台），
// 然后通过 fork 创建子进程执行命令树，父进程 wait 等待结果。
// 唯一特殊的是 cd，它必须由 shell 自己调用 chdir，
// 因为 chdir 只影响调用进程，放在子进程里不会改变 shell 的目录。

#include "kernel/types.h"
#include "user/user.h"
#include "kernel/fcntl.h"
#include "kernel/signal.h"
#include "kernel/stat.h"

// 命令树节点类型。
#define EXEC  1
#define REDIR 2
#define PIPE  3
#define LIST  4
#define BACK  5
#define AND   6

#define MAXARGS 10

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

struct andcmd {
  int type;
  struct cmd *left;
  struct cmd *right;
};

#define MAXJOBS 16
#define JOB_RUNNING 0
#define JOB_STOPPED 1

struct job {
  int used;
  int pid;
  int state;
  char cmd[100];
};

static struct job jobs[MAXJOBS];

int fork1(void);  // Fork but panics on failure.
void panic(char*);
struct cmd *parsecmd(char*);
void runcmd(struct cmd*) __attribute__((noreturn));

// 执行命令树。正常情况下不会返回。
void
runcmd(struct cmd *cmd)
{
  int p[2];
  struct andcmd *acmd;
  struct backcmd *bcmd;
  struct execcmd *ecmd;
  struct listcmd *lcmd;
  struct pipecmd *pcmd;
  struct redircmd *rcmd;
  int astatus;

  if(cmd == 0)
    exit(1);

  switch(cmd->type){
  default:
    panic("runcmd");

  case EXEC:
    // 普通命令：直接用 exec 替换当前进程。
    ecmd = (struct execcmd*)cmd;
    if(ecmd->argv[0] == 0)
      exit(1);
    exec(ecmd->argv[0], ecmd->argv);
    fprintf(2, "exec %s failed\n", ecmd->argv[0]);
    exit(1);

  case REDIR:
    // 重定向：先把目标文件打开到指定 fd，再执行子命令。
    rcmd = (struct redircmd*)cmd;
    close(rcmd->fd);
    if(open(rcmd->file, rcmd->mode) < 0){
      fprintf(2, "open %s failed\n", rcmd->file);
      exit(1);
    }
    runcmd(rcmd->cmd);
    break;

  case LIST:
    // 顺序执行：先运行左边，等待结束后再运行右边。
    lcmd = (struct listcmd*)cmd;
    if(fork1() == 0)
      runcmd(lcmd->left);
    wait(0);
    runcmd(lcmd->right);
    break;

  case PIPE:
    // 管道：创建 pipe，左命令标准输出接写端，右命令标准输入接读端。
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
    // 后台命令：fork 子进程执行，父进程不等待。
    bcmd = (struct backcmd*)cmd;
    if(fork1() == 0)
      runcmd(bcmd->cmd);
    break;

  case AND:
    // 短路执行：左边成功才运行右边。
    acmd = (struct andcmd*)cmd;
    if(fork1() == 0)
      runcmd(acmd->left);
    wait(&astatus);
    if(astatus == 0){
      if(fork1() == 0)
        runcmd(acmd->right);
      wait(0);
    }
    break;
  }
  exit(0);
}

int
getcmd(char *buf, int nbuf)
{
  write(2, "$ ", 2);
  memset(buf, 0, nbuf);
  gets(buf, nbuf);
  if(buf[0] == 0) // 读到 EOF，结束 shell
    return -1;
  return 0;
}

static struct job*
addjob(int pid, int state, char *cmd)
{
  for(int i = 0; i < MAXJOBS; i++){
    if(jobs[i].used)
      continue;
    jobs[i].used = 1;
    jobs[i].pid = pid;
    jobs[i].state = state;
    int n = 0;
    while(cmd[n] && n < sizeof(jobs[i].cmd) - 1){
      jobs[i].cmd[n] = cmd[n];
      n++;
    }
    jobs[i].cmd[n] = 0;
    return &jobs[i];
  }
  return 0;
}

static void
removejob(struct job *j)
{
  if(j)
    j->used = 0;
}

static struct job*
findjob(int n)
{
  if(n <= 0 || n > MAXJOBS || !jobs[n - 1].used)
    return 0;
  return &jobs[n - 1];
}

static void
listjobs(void)
{
  for(int i = 0; i < MAXJOBS; i++){
    if(!jobs[i].used)
      continue;
    printf("[%d] %d %s %s", i + 1, jobs[i].pid,
           jobs[i].state == JOB_STOPPED ? "stopped" : "running",
           jobs[i].cmd);
  }
}

static void
reap_jobs(void)
{
  for(int i = 0; i < MAXJOBS; i++){
    struct job *j = &jobs[i];
    int st;

    if(!j->used)
      continue;
    int r = waitpid_flags(j->pid, &st, WNOHANG);
    if(r == j->pid){
      if(XV6_WIFSTOPPED(st))
        j->state = JOB_STOPPED;
      else if(XV6_WIFCONTINUED(st))
        j->state = JOB_RUNNING;
      else
        removejob(j);
    }
  }
}

static int
jobnum(char *s)
{
  while(*s == ' ' || *s == '\t')
    s++;
  if(*s == '%')
    s++;
  return atoi(s);
}

static int
startswith(char *s, char *p)
{
  while(*p){
    if(*s++ != *p++)
      return 0;
  }
  return 1;
}

int
main(void)
{
  static char buf[100];
  int fd;

  // 保证标准输入/输出/错误三个描述符可用，否则后续命令无法工作。
  while((fd = open("console", O_RDWR)) >= 0){
    if(fd >= 3){
      close(fd);
      break;
    }
  }
  signal(SIGTSTP, SIG_IGN);
  signal(SIGINT, SIG_IGN);

  // 主循环：读一行命令，解析并执行。
  while(getcmd(buf, sizeof(buf)) >= 0){
    char *cmd = buf;
    reap_jobs();
    while (*cmd == ' ' || *cmd == '\t')
      cmd++;
    if (*cmd == '\n') // is a blank command
      continue;
    if(cmd[0] == 'c' && cmd[1] == 'd' && cmd[2] == ' '){
      // cd 必须由 shell 父进程执行，而不是在子进程中。
      cmd[strlen(cmd)-1] = 0;  // chop \n
      if(chdir(cmd+3) < 0)
        fprintf(2, "cannot cd %s\n", cmd+3);
    } else if(startswith(cmd, "jobs") &&
              (cmd[4] == '\n' || cmd[4] == ' ' || cmd[4] == 0)){
      listjobs();
    } else if(startswith(cmd, "fg ") || startswith(cmd, "fg%")){
      struct job *j = findjob(jobnum(cmd + 2));
      int st;
      if(j == 0){
        fprintf(2, "fg: no such job\n");
        continue;
      }
      if(j->state == JOB_STOPPED && killpg(j->pid, SIGCONT) < 0){
        fprintf(2, "fg: killpg failed\n");
        continue;
      }
      j->state = JOB_RUNNING;
      tcsetpgrp(0, j->pid);
      for(;;){
        int r = waitpid_flags(j->pid, &st, WUNTRACED | WCONTINUED);
        if(r != j->pid){
          removejob(j);
          break;
        }
        if(XV6_WIFSTOPPED(st)){
          j->state = JOB_STOPPED;
          break;
        }
        if(XV6_WIFCONTINUED(st)){
          j->state = JOB_RUNNING;
          continue;
        }
        removejob(j);
        break;
      }
      tcsetpgrp(0, getpgid(0));
    } else if(startswith(cmd, "bg ") || startswith(cmd, "bg%")){
      struct job *j = findjob(jobnum(cmd + 2));
      if(j == 0){
        fprintf(2, "bg: no such job\n");
        continue;
      }
      if(j->state == JOB_STOPPED && killpg(j->pid, SIGCONT) < 0){
        fprintf(2, "bg: killpg failed\n");
        continue;
      }
      j->state = JOB_RUNNING;
      printf("[%d] %d running %s", (int)(j - jobs) + 1, j->pid, j->cmd);
    } else if(startswith(cmd, "stop ") || startswith(cmd, "stop%")){
      struct job *j = findjob(jobnum(cmd + 4));
      if(j == 0){
        fprintf(2, "stop: no such job\n");
        continue;
      }
      if(killpg(j->pid, SIGSTOP) < 0){
        fprintf(2, "stop: killpg failed\n");
        continue;
      }
      j->state = JOB_STOPPED;
      printf("[%d] %d stopped %s", (int)(j - jobs) + 1, j->pid, j->cmd);
    } else {
      char childcmd[100];
      int n = 0;
      while(cmd[n] && n < sizeof(childcmd) - 1){
        childcmd[n] = cmd[n];
        n++;
      }
      childcmd[n] = 0;
      int bg = 0;
      for(int i = n - 1; i >= 0; i--){
        if(childcmd[i] == ' ' || childcmd[i] == '\t' ||
           childcmd[i] == '\n')
          continue;
        if(childcmd[i] == '&' && (i == 0 || childcmd[i - 1] != '&'))
          bg = 1;
        break;
      }
      if(bg){
        for(int i = n - 1; i >= 0; i--){
          if(childcmd[i] == '&' && (i == 0 || childcmd[i - 1] != '&')){
            childcmd[i] = '\n';
            break;
          }
        }
      }
      int pid = fork1();
      if(pid == 0){
        signal(SIGTSTP, SIG_DFL);
        signal(SIGINT, SIG_DFL);
        if(setpgid(0, 0) < 0)
          fprintf(2, "setpgid failed\n");
        runcmd(parsecmd(childcmd));
      }
      if(setpgid(pid, pid) < 0)
        fprintf(2, "setpgid failed\n");
      struct job *j = addjob(pid, JOB_RUNNING, cmd);
      if(!bg){
        int st;
        tcsetpgrp(0, pid);
        for(;;){
          int r = waitpid_flags(pid, &st, WUNTRACED | WCONTINUED);
          if(r != pid){
            removejob(j);
            break;
          }
          if(XV6_WIFSTOPPED(st)){
            if(j)
              j->state = JOB_STOPPED;
            break;
          }
          if(XV6_WIFCONTINUED(st)){
            if(j)
              j->state = JOB_RUNNING;
            continue;
          }
          removejob(j);
          break;
        }
        tcsetpgrp(0, getpgid(0));
      } else if(j){
        printf("[%d] %d running %s", (int)(j - jobs) + 1, j->pid, j->cmd);
      }
    }
  }
  exit(0);
}

void
panic(char *s)
{
  fprintf(2, "%s\n", s);
  exit(1);
}

int
fork1(void)
{
  int pid;

  pid = fork();
  if(pid == -1)
    panic("fork");
  return pid;
}

//PAGEBREAK!
// 命令树节点构造函数。

struct cmd*
execcmd(void)
{
  struct execcmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = EXEC;
  return (struct cmd*)cmd;
}

struct cmd*
redircmd(struct cmd *subcmd, char *file, char *efile, int mode, int fd)
{
  struct redircmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = REDIR;
  cmd->cmd = subcmd;
  cmd->file = file;
  cmd->efile = efile;
  cmd->mode = mode;
  cmd->fd = fd;
  return (struct cmd*)cmd;
}

struct cmd*
pipecmd(struct cmd *left, struct cmd *right)
{
  struct pipecmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = PIPE;
  cmd->left = left;
  cmd->right = right;
  return (struct cmd*)cmd;
}

struct cmd*
listcmd(struct cmd *left, struct cmd *right)
{
  struct listcmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = LIST;
  cmd->left = left;
  cmd->right = right;
  return (struct cmd*)cmd;
}

struct cmd*
backcmd(struct cmd *subcmd)
{
  struct backcmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = BACK;
  cmd->cmd = subcmd;
  return (struct cmd*)cmd;
}

struct cmd*
andcmd(struct cmd *left, struct cmd *right)
{
  struct andcmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = AND;
  cmd->left = left;
  cmd->right = right;
  return (struct cmd*)cmd;
}
//PAGEBREAK!
// 命令行解析：把输入字符串拆成 token，构造命令树。

char whitespace[] = " \t\r\n\v";
char symbols[] = "<|>&;()";

int
gettoken(char **ps, char *es, char **q, char **eq)
{
  char *s;
  int ret;

  s = *ps;
  // 跳过空白，定位 token 起点。
  while(s < es && strchr(whitespace, *s))
    s++;
  if(q)
    *q = s;
  ret = *s;
  switch(*s){
  case 0:
    break;
  case '|':
  case '(':
  case ')':
  case ';':
  case '<':
    s++;
    break;
  case '&':
    s++;
    if(*s == '&'){
      ret = 'A';  // "&&" 表示逻辑与
      s++;
    }
    break;
  case '>':
    s++;
    if(*s == '>'){  // ">>" 表示追加模式
      ret = '+';
      s++;
    }
    break;
  default:  // 普通单词
    ret = 'a';
    while(s < es && !strchr(whitespace, *s) && !strchr(symbols, *s))
      s++;
    break;
  }
  if(eq)
    *eq = s;

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

struct cmd *parseline(char**, char*);
struct cmd *parsepipe(char**, char*);
struct cmd *parseexec(char**, char*);
struct cmd *nulterminate(struct cmd*);

struct cmd*
parsecmd(char *s)
{
  char *es;
  struct cmd *cmd;

  es = s + strlen(s);
  cmd = parseline(&s, es);
  peek(&s, es, "");
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
    int tok = gettoken(ps, es, 0, 0);
    if(tok == 'A')
      cmd = andcmd(cmd, parseline(ps, es));
    else
      cmd = backcmd(cmd);
  }
  if(peek(ps, es, ";")){
    gettoken(ps, es, 0, 0);
    cmd = listcmd(cmd, parseline(ps, es));
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
    cmd = pipecmd(cmd, parsepipe(ps, es));
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
      cmd = redircmd(cmd, q, eq, O_RDONLY, 0);
      break;
    case '>':
      cmd = redircmd(cmd, q, eq, O_WRONLY|O_CREATE|O_TRUNC, 1);
      break;
    case '+':  // >>
      cmd = redircmd(cmd, q, eq, O_WRONLY|O_CREATE, 1);
      break;
    }
  }
  return cmd;
}

struct cmd*
parseblock(char **ps, char *es)
{
  struct cmd *cmd;

  if(!peek(ps, es, "("))
    panic("parseblock");
  gettoken(ps, es, 0, 0);
  cmd = parseline(ps, es);
  if(!peek(ps, es, ")"))
    panic("syntax - missing )");
  gettoken(ps, es, 0, 0);
  cmd = parseredirs(cmd, ps, es);
  return cmd;
}

struct cmd*
parseexec(char **ps, char *es)
{
  char *q, *eq;
  int tok, argc;
  struct execcmd *cmd;
  struct cmd *ret;

  if(peek(ps, es, "("))
    return parseblock(ps, es);

  ret = execcmd();
  cmd = (struct execcmd*)ret;

  argc = 0;
  ret = parseredirs(ret, ps, es);
  while(!peek(ps, es, "|)&;")){
    if((tok=gettoken(ps, es, &q, &eq)) == 0)
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

// 解析时只记录了每个字符串的起止位置；最后统一补上 '\0'。
struct cmd*
nulterminate(struct cmd *cmd)
{
  int i;
  struct andcmd *acmd;
  struct backcmd *bcmd;
  struct execcmd *ecmd;
  struct listcmd *lcmd;
  struct pipecmd *pcmd;
  struct redircmd *rcmd;

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

  case AND:
    acmd = (struct andcmd*)cmd;
    nulterminate(acmd->left);
    nulterminate(acmd->right);
    break;
  }
  return cmd;
}
