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
#define OR    7

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

struct orcmd {
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
static void addhistory(char *s);
static int sh_readline(char *buf, int nbuf);

// 执行命令树。正常情况下不会返回。
void
runcmd(struct cmd *cmd)
{
  int p[2];
  struct andcmd *acmd;
  struct orcmd *ocmd;
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
    if(ecmd->argv[0][0] != '/' && !strchr(ecmd->argv[0], '/')){
      char full[100];
      int n = strlen(ecmd->argv[0]);
      if(n + 2 < (int)sizeof(full)){
        full[0] = '/';
        memmove(full + 1, ecmd->argv[0], n + 1);
        exec(full, ecmd->argv);
      }
    }
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

  case OR:
    // 左边失败才运行右边。
    ocmd = (struct orcmd*)cmd;
    if(fork1() == 0)
      runcmd(ocmd->left);
    wait(&astatus);
    if(astatus != 0){
      if(fork1() == 0)
        runcmd(ocmd->right);
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
  if(sh_readline(buf, nbuf) < 0)
    return -1;
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

static int
iscmd(char *s, char *name)
{
  int n = 0;

  while(name[n]){
    if(s[n] != name[n])
      return 0;
    n++;
  }
  return s[n] == '\n' || s[n] == ' ' || s[n] == '\t' || s[n] == 0;
}

#define MAXHIST 16
#define MAXVARS 16
#define MAXALIAS 16

static char history[MAXHIST][100];
static int nhistory;
static char vars[MAXVARS][64];
static int nvars;
static char aliases[MAXALIAS][100];
static int naliases;

static void
addhistory(char *s)
{
  char *p = s;
  int n = 0;

  while(*p == ' ' || *p == '\t')
    p++;
  if(*p == 0 || *p == '\n')
    return;
  if(nhistory == MAXHIST){
    for(int i = 1; i < MAXHIST; i++)
      memmove(history[i - 1], history[i], sizeof(history[i]));
    nhistory--;
  }
  while(*p && n < (int)sizeof(history[nhistory]) - 1){
    history[nhistory][n++] = *p++;
  }
  history[nhistory][n] = 0;
  nhistory++;
}

static void
expand_history(char *buf, int nbuf)
{
  char tmp[256];
  int n;

  if(buf[0] != '!' || nhistory == 0)
    return;
  if(buf[1] == '!'){
    n = nhistory - 1;
  } else if(buf[1] >= '0' && buf[1] <= '9'){
    n = atoi(buf + 1) - 1;
    if(n < 0 || n >= nhistory)
      return;
  } else {
    return;
  }
  int len = strlen(history[n]);
  if(len > 0 && history[n][len - 1] == '\n')
    len--;
  int rest = strlen(buf + (buf[1] == '!' ? 2 : 1 + (buf[1] >= '0' && buf[1] <= '9' ? 1 : 0)));
  if(len + rest + 1 > (int)sizeof(tmp))
    return;
  memmove(tmp, history[n], len);
  memmove(tmp + len, buf + (buf[1] == '!' ? 2 : 1 + (buf[1] >= '0' && buf[1] <= '9' ? 1 : 0)), rest + 1);
  if(len + rest + 1 <= nbuf)
    memmove(buf, tmp, len + rest + 1);
}

static void
setvar(char *name, char *value)
{
  char entry[64];
  int nl = strlen(name);
  int vl = strlen(value);

  if(nl == 0 || nl + vl + 2 > (int)sizeof(entry))
    return;
  memmove(entry, name, nl);
  entry[nl] = '=';
  memmove(entry + nl + 1, value, vl + 1);
  for(int i = 0; i < nvars; i++){
    if(startswith(vars[i], name) && vars[i][nl] == '='){
      memmove(vars[i], entry, strlen(entry) + 1);
      return;
    }
  }
  if(nvars < MAXVARS)
    memmove(vars[nvars++], entry, strlen(entry) + 1);
}

static int
unsetvar(char *name)
{
  int nl = strlen(name);

  for(int i = 0; i < nvars; i++){
    if(startswith(vars[i], name) && vars[i][nl] == '='){
      memmove(vars[i], vars[nvars - 1], strlen(vars[nvars - 1]) + 1);
      nvars--;
      return 0;
    }
  }
  return -1;
}

static void
expand_vars(char *out, int max, char *in)
{
  int oi = 0;

  for(int i = 0; in[i] && oi < max - 1; i++){
    if(in[i] == '$' && (in[i + 1] == '_' ||
       (in[i + 1] >= 'a' && in[i + 1] <= 'z') ||
       (in[i + 1] >= 'A' && in[i + 1] <= 'Z') ||
       (in[i + 1] >= '0' && in[i + 1] <= '9'))){
      int j = i + 1;
      while(in[j] == '_' ||
            (in[j] >= 'a' && in[j] <= 'z') ||
            (in[j] >= 'A' && in[j] <= 'Z') ||
            (in[j] >= '0' && in[j] <= '9'))
        j++;
      char name[32];
      int nl = j - (i + 1);
      if(nl < (int)sizeof(name)){
        memmove(name, in + i + 1, nl);
        name[nl] = 0;
        for(int v = 0; v < nvars; v++){
          if(startswith(vars[v], name) && vars[v][nl] == '='){
            char *val = vars[v] + nl + 1;
            int vl = strlen(val);
            if(oi + vl < max - 1){
              memmove(out + oi, val, vl);
              oi += vl;
            }
            i = j - 1;
            goto next;
          }
        }
      }
      out[oi++] = '$';
      continue;
    }
    out[oi++] = in[i];
next:;
  }
  out[oi] = 0;
}

static void
setalias(char *name, char *value)
{
  char entry[100];
  int nl = strlen(name);
  int vl = strlen(value);

  if(nl == 0 || nl + vl + 2 > (int)sizeof(entry))
    return;
  memmove(entry, name, nl);
  entry[nl] = '=';
  memmove(entry + nl + 1, value, vl + 1);
  for(int i = 0; i < naliases; i++){
    if(startswith(aliases[i], name) && aliases[i][nl] == '='){
      memmove(aliases[i], entry, strlen(entry) + 1);
      return;
    }
  }
  if(naliases < MAXALIAS)
    memmove(aliases[naliases++], entry, strlen(entry) + 1);
}

static int
unsetalias(char *name)
{
  int nl = strlen(name);

  for(int i = 0; i < naliases; i++){
    if(startswith(aliases[i], name) && aliases[i][nl] == '='){
      memmove(aliases[i], aliases[naliases - 1], strlen(aliases[naliases - 1]) + 1);
      naliases--;
      return 0;
    }
  }
  return -1;
}

static void
expand_aliases(char *out, int max, char *in)
{
  char *s = in;
  int first = 0;

  while(*s == ' ' || *s == '\t')
    s++;
  first = s - in;
  char *e = s;
  while(*e && *e != ' ' && *e != '\t' && *e != '\n')
    e++;
  int nl = e - s;
  for(int i = 0; i < naliases; i++){
    if(memcmp(aliases[i], s, nl) == 0 && aliases[i][nl] == '='){
      char *val = aliases[i] + nl + 1;
      int vl = strlen(val);
      int rest = strlen(e);
      if(first + vl + rest + 1 > max)
        break;
      memmove(out, in, first);
      memmove(out + first, val, vl);
      memmove(out + first + vl, e, rest + 1);
      return;
    }
  }
  memmove(out, in, strlen(in) + 1);
}

static int
hist_copy(int idx, char *dst, int max)
{
  int i = 0;

  while(i + 1 < max && history[idx][i] && history[idx][i] != '\n'){
    dst[i] = history[idx][i];
    i++;
  }
  dst[i] = 0;
  return i;
}

static void
line_redraw(int oldlen, char *line, int newlen)
{
  for(int i = 0; i < oldlen; i++)
    write(2, "\b", 1);
  if(newlen > 0)
    write(2, line, newlen);
  for(int i = newlen; i < oldlen; i++)
    write(2, " ", 1);
  for(int i = newlen; i < oldlen; i++)
    write(2, "\b", 1);
}

static int
sh_readline(char *buf, int nbuf)
{
  char line[256];
  int len = 0;
  int hindex = nhistory;
  char c;

  line[0] = 0;
  for(;;){
    int cc = read(0, &c, 1);
    if(cc < 1)
      return -1;
    if(c == '\n' || c == '\r')
      break;
    if(c == 0x08 || c == 0x7f){
      if(len > 0){
        write(2, "\b \b", 3);
        len--;
        line[len] = 0;
      }
      continue;
    }
    if(c == 0x15){ // Ctrl-U: 清除当前输入。
      if(len > 0){
        line_redraw(len, line, 0);
        len = 0;
        line[0] = 0;
      }
      continue;
    }
    if(c == 0x01){ // Up: 上一条历史。
      if(hindex > 0){
        hindex--;
        int old = len;
        len = hist_copy(hindex, line, sizeof(line));
        line_redraw(old, line, len);
      }
      continue;
    }
    if(c == 0x02){ // Down: 下一条历史。
      if(hindex < nhistory){
        hindex++;
        int old = len;
        if(hindex == nhistory){
          line[0] = 0;
          len = 0;
        } else {
          len = hist_copy(hindex, line, sizeof(line));
        }
        line_redraw(old, line, len);
      }
      continue;
    }
    if(c == 0x04)
      return -1;
    if(c >= 0x20 && len + 1 < nbuf - 1){
      line[len++] = c;
      line[len] = 0;
    }
  }
  if(len + 1 >= nbuf)
    return -1;
  memmove(buf, line, len);
  buf[len] = '\n';
  buf[len + 1] = 0;
  return 0;
}

int
main(void)
{
  static char buf[256];
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
    char expanded[256];
    char *cmd = buf;
    expand_history(buf, sizeof(buf));
    expand_aliases(expanded, sizeof(expanded), buf);
    expand_vars(buf, sizeof(buf), expanded);
    addhistory(buf);
    reap_jobs();
    while (*cmd == ' ' || *cmd == '\t')
      cmd++;
    if (*cmd == '\n') // is a blank command
      continue;
    if(iscmd(cmd, "exit"))
      break;
    if(iscmd(cmd, "pwd")){
      char cwd[MAXPATH];
      if(getcwd(cwd, sizeof(cwd)) == 0)
        printf("%s\n", cwd);
      else
        fprintf(2, "pwd: failed\n");
      continue;
    }
    if(iscmd(cmd, "history")){
      for(int i = 0; i < nhistory; i++)
        printf("%d  %s", i + 1, history[i]);
      continue;
    }
    if(startswith(cmd, "export ") || iscmd(cmd, "export")){
      char *p = cmd + 6;
      while(*p == ' ' || *p == '\t')
        p++;
      char *eq = strchr(p, '=');
      if(eq){
        *eq = 0;
        char *v = eq + 1;
        while(*v && *v != '\n')
          v++;
        *v = 0;
        setvar(p, eq + 1);
      } else if(*p && *p != '\n'){
        for(int i = 0; i < nvars; i++)
          printf("%s\n", vars[i]);
      }
      continue;
    }
    if(startswith(cmd, "unset ")){
      char *p = cmd + 6;
      while(*p == ' ' || *p == '\t')
        p++;
      char *e = p;
      while(*e && *e != '\n' && *e != ' ' && *e != '\t')
        e++;
      *e = 0;
      unsetvar(p);
      continue;
    }
    if(iscmd(cmd, "vars")){
      for(int i = 0; i < nvars; i++)
        printf("%s\n", vars[i]);
      continue;
    }
    if(startswith(cmd, "alias ") || iscmd(cmd, "alias")){
      char *p = cmd + 5;
      while(*p == ' ' || *p == '\t')
        p++;
      char *eq = strchr(p, '=');
      if(eq){
        *eq = 0;
        char *v = eq + 1;
        while(*v && *v != '\n')
          v++;
        *v = 0;
        setalias(p, eq + 1);
      } else {
        for(int i = 0; i < naliases; i++)
          printf("%s\n", aliases[i]);
      }
      continue;
    }
    if(startswith(cmd, "unalias ")){
      char *p = cmd + 8;
      while(*p == ' ' || *p == '\t')
        p++;
      char *e = p;
      while(*e && *e != '\n' && *e != ' ' && *e != '\t')
        e++;
      *e = 0;
      unsetalias(p);
      continue;
    }
    if(iscmd(cmd, "aliases")){
      for(int i = 0; i < naliases; i++)
        printf("%s\n", aliases[i]);
      continue;
    }
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
      char childcmd[256];
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

struct cmd*
orcmd(struct cmd *left, struct cmd *right)
{
  struct orcmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = OR;
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
    s++;
    if(*s == '|'){
      ret = 'O';  // "||" 表示逻辑或
      s++;
    }
    break;
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
  while(peek(ps, es, "&|")){
    int tok = gettoken(ps, es, 0, 0);
    if(tok == 'A')
      cmd = andcmd(cmd, parseline(ps, es));
    else if(tok == 'O')
      cmd = orcmd(cmd, parseline(ps, es));
    else if(tok == '|'){
      fprintf(2, "syntax: unmatched |\n");
      panic("syntax");
    }
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
  struct orcmd *ocmd;
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

  case OR:
    ocmd = (struct orcmd*)cmd;
    nulterminate(ocmd->left);
    nulterminate(ocmd->right);
    break;
  }
  return cmd;
}
