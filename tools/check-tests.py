#!/usr/bin/env python3
import re
import sys
from pathlib import Path

root = Path(__file__).resolve().parent.parent

def parse_syscalls():
    text = (root / "kernel/syscall.h").read_text()
    return set(re.findall(r"#define\s+(SYS_[A-Z0-9_]+)\s+\d+", text))

def parse_syscall_names():
    text = (root / "kernel/syscall_names.h").read_text()
    return set(re.findall(r"X\(\s*(SYS_[A-Z0-9_]+)", text))

def parse_module_ids():
    text = (root / "kernel/module/module_ids.h").read_text()
    ids = {}
    for name, value in re.findall(r"#define\s+(KMOD_[A-Z0-9_]+)\s+(\d+)", text):
        if name.endswith("_MAJOR"):
            continue
        ids.setdefault(int(value), []).append(name)
    return ids

def main():
    ok = True

    syscalls = parse_syscalls()
    names = parse_syscall_names()
    missing = syscalls - names
    extra = names - syscalls
    if missing:
        ok = False
        print("missing syscall names:", ", ".join(sorted(missing)))
    if extra:
        ok = False
        print("unknown syscall names:", ", ".join(sorted(extra)))

    ids = parse_module_ids()
    for value, names in ids.items():
        if len(names) > 1:
            ok = False
            print(f"duplicate module id {value}: {', '.join(names)}")

    if not ok:
        sys.exit(1)
    print("host checks OK")

if __name__ == "__main__":
    main()
