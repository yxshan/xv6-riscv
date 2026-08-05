#!/usr/bin/env python3

#
# python script that tests xv6 without having to boot it and type to its shell
#
# ./test-xv6.py usertests  (runs usertests)
# ./test-xv6.py -q usertests (runs the quick tests of usertests)
# ./test-xv6.py crash  (runs the crash tests)
# ./test-xv6.py log (runs the log crash test)

import argparse, os, inspect, re, signal, subprocess, sys, time
from subprocess import run

parser = argparse.ArgumentParser()
parser.add_argument('testrex', help="test name or regular expression")
parser.add_argument("-q", action='store_true', help="usertests quick")
args = parser.parse_args()

class QEMU(object):

    def __init__(self, reset=False):
        if reset:
            self.build_xv6()
            self.reset_fs()
        q = ["make", "qemu"]
        self.proc = subprocess.Popen(q, stdin=subprocess.PIPE,
                                      stdout=subprocess.PIPE,
                                      stderr=subprocess.STDOUT,
                                      start_new_session=True)
        self.output = ""
        self.outbytes = bytearray()       
        time.sleep(1)

    def reset_fs(self):
        try:
            run(["rm", "fs.img"], check=True)
            run(["make", "fs.img"], check=True)
        except subprocess.CalledProcessError as e:
            print(f"Command failed with exit code {e.returncode}")

    def build_xv6(self):
        try:
            run(["make", "kernel/kernel"], check=True)
        except subprocess.CalledProcessError as e:
            print(f"Command failed with exit code {e.returncode}")

    def save_output(self):
      try:
        with open("test-xv6.out", "w") as f:
            f.write(self.out)
            f.close()
      except OSError as e:
        print("Provided a bad results path. Error:", e)     
        
    def cmd(self, c):
        if isinstance(c, str):
            c = c.encode('utf-8')
        self.proc.stdin.write(c)
        self.proc.stdin.flush()
        
    def crash(self):
        pg = run(['pgrep', '-f', 'qemu-system-riscv64.*fs.img'],
                 stdout=subprocess.PIPE, encoding='utf8')
        kids = [int(line) for line in pg.stdout.splitlines()]
        if len(kids) == 0:
            print("no qemu")
            sys.exit(1)
        for pid in kids:
            print("kill", pid)
            os.kill(pid, signal.SIGKILL)

    def stop(self):
        if self.proc.poll() is None:
            os.killpg(os.getpgid(self.proc.pid), signal.SIGTERM)
            self.proc.wait()

    def read(self):
        buf = os.read(self.proc.stdout.fileno(), 4096)
        self.outbytes.extend(buf)
        self.output = self.outbytes.decode("utf-8", "replace")

    def lines(self):
        return self.output.splitlines()

    def error(self):
        print("FAIL: match failed")
        self.save_output()
        self.stop()
        sys.exit(1)

    def match(self, *regexps, exit=True):
        lines = self.lines()
        last = -1
        for i, line in enumerate(lines):
            if any(re.match(r, line) for r in regexps):
                print(line)
                last = i
        if last == -1 and exit:
            self.error()
        l = ""
        if last >= 0:
            l = lines[last]
        return last >= 0, l

    def monitor(self, *regexps, progress="", timeout):
        deadline = time.time() + timeout
        progress_idx = 0
        while True:
            ok, _ = self.match(*regexps, exit=False)
            if ok:
                return
            if progress:
                lines = self.lines()
                for line in lines[progress_idx:]:
                    if re.match(progress, line):
                        print(line)
                progress_idx = len(lines)
            time.sleep(1)
            timeleft = deadline - time.time()
            if timeleft < 0:
                self.error()
            self.read()
            ok, _ = self.match(*regexps, exit=False)
            if ok:
                return
            if progress:
                lines = self.lines()
                for line in lines[progress_idx:]:
                    if re.match(progress, line):
                        print(line)
                progress_idx = len(lines)

def crash_log():
    q = QEMU(True)
    q.cmd("logstress f0 f1 f2 f3\n")
    q.monitor("^logstress start", timeout=30)
    time.sleep(8)
    q.crash()
    q.stop()

def recover_log():
    q = QEMU()
    time.sleep(3)
    q.read()
    ok, _ = q.match('^recovering', exit=False)
    if ok:
        q.cmd("ls\n")
        time.sleep(2)
        q.read()
        q.match('f3')
    else:
        print("recover output:", q.output[-500:])
    q.stop()
    return ok

def forphan():
    q = QEMU(True)
    q.cmd("forphan\n")
    time.sleep(5)
    q.read()
    q.match('wait')
    q.crash()
    q.stop()

def dorphan():
    q = QEMU(True)
    q.cmd("dorphan\n")
    time.sleep(5)
    q.read()
    q.match('wait')
    q.crash()
    q.stop()

def recover_orphan():
    q = QEMU()
    time.sleep(3)
    q.read()
    q.match('^ireclaim')
    q.stop()

def test_log():
    print("Test recovery of log")
    for i in range(5):
        crash_log()
        ok = recover_log()
        if ok:
            print("OK")
            return
        print("log attempt ", i+1)
    print("FAIL")
    sys.exit(1)
    
def test_forphan():
    print("Test recovery of an orphaned file")
    forphan()
    recover_orphan()
    print("OK")

def test_dorphan():
    print("Test recovery of an orphaned file")
    dorphan()
    recover_orphan()
    print("OK")

def test_crash():
    test_log()
    test_forphan()
    test_dorphan()

def test_usertests(test=""):
    timeout = 600
    opt = ""
    if args.q:
        opt = " -q"
        timeout = 300
    elif test != "":
        opt += " " + test
    q = QEMU(True)
    q.cmd("usertests" + opt + "\n")
    q.monitor('^ALL TESTS PASSED', progress='^test .*: (OK|FAILED)$', timeout=timeout)
    q.stop()

def test_tools():
    print("Test standalone user tools")
    q = QEMU(True)
    q.cmd("cowtest\n")
    q.monitor("^COW OK", timeout=60)
    q.cmd("shmtest\n")
    q.monitor("^shared=SHM", timeout=60)
    q.cmd("signaltest\n")
    q.monitor("^handler sig=10", timeout=60)
    q.cmd("strace echo hi\n")
    q.monitor("^strace: syscalls=", timeout=60)
    q.cmd("perf echo hi\n")
    q.monitor("^perf: syscalls=", timeout=60)
    q.cmd("procinfo\n")
    q.monitor("^pid ", timeout=60)
    q.cmd("crashdump\n")
    q.monitor("^=== kernel crash dump ===", timeout=60)
    q.cmd("prio 2 10\n")
    q.monitor("^setpriority\\(2, 10\\) = 0", timeout=60)
    q.cmd("echo one && echo two\n")
    q.monitor("^one", timeout=60)
    q.monitor("^two", timeout=60)
    q.cmd("ln -s README.md slink\ncat slink\n")
    q.monitor(".*# xv6-riscv", timeout=60)
    q.cmd("mkfifo fifo\ncat fifo &\necho hello > fifo\n")
    q.monitor(".*hello", timeout=60)
    q.cmd("ps\n")
    q.monitor("^pid .* ps", timeout=60)
    q.cmd("id\n")
    q.monitor("^\\$ uid=0 gid=0 euid=0 egid=0|^uid=0 gid=0 euid=0 egid=0", timeout=60)
    q.cmd("ls /disk1\n")
    q.monitor("^echo ", timeout=60)
    q.cmd("cat /disk1/README.md\n")
    q.monitor(".*# xv6-riscv", timeout=60)
    q.cmd("echo x > /disk1/readonly\n")
    q.monitor(".*open /disk1/readonly failed", timeout=60)
    q.stop()
    print("OK")

def test_grind():
    print("Test grind stress")
    q = QEMU(True)
    q.cmd("grind\n")
    time.sleep(8)
    q.read()
    bad, _ = q.match("grind: .* failed|^panic", exit=False)
    if bad:
        q.stop()
        print("FAIL")
        sys.exit(1)
    q.stop()
    print("OK")

def test_modules():
    print("Test dynamic module lifecycle")
    q = QEMU(True)
    q.cmd("modload dynmod\n")
    q.monitor("^module_load = 0", timeout=60)
    q.cmd("modcli 3 1\n")
    q.monitor("^module_call\\(3, 1\\) = 4660", timeout=60)
    q.cmd("modunload\n")
    q.monitor("^dynmod unloaded", timeout=60)
    q.monitor("^module_unload = 0", timeout=60)
    q.stop()
    print("OK")

def main():
    print(args)
    rex = r'%s' % args.testrex
    funcs = [(obj,name) for name,obj in inspect.getmembers(sys.modules[__name__]) 
                     if (inspect.isfunction(obj) and 
                         name.startswith('test'))]
    none = True
    for (f,n) in funcs:
        if re.search(rex, n):
            none = False
            f()
    if none:
        test_usertests(test=args.testrex)

main()
