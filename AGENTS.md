# Repository Guidelines

## Project Structure & Module Organization

- `kernel/`: RISC-V kernel C and `.S` sources, headers, and `kernel.ld`; `kernel/defs.h` declares cross-module functions.
- `user/`: user programs and library; `user/foo.c` builds into `user/_foo` for `fs.img`.
- `mkfs/mkfs.c`: host-side tool that builds the filesystem image.
- Root: `Makefile`, `README`, `test-xv6.py`, plus analysis docs `xv6-riscv-source-analysis.md` and `xv6-riscv-extension-ideas.md`.

## Build, Test, and Development Commands

Requires a RISC-V newlib toolchain and `qemu-system-riscv64` 7.2+; override `TOOLPREFIX` for custom toolchains.

- `make qemu`: build and boot xv6 in QEMU.
- `make kernel/kernel`: build the kernel only for faster iteration.
- `make fs.img`: rebuild the filesystem image.
- `make qemu-gdb`: boot under a GDB stub; `make print-gdbport` prints the port.
- `make clean`: remove build artifacts. `make tags`: regenerate etags.
- `./test-xv6.py usertests`: run the full in-guest suite; add `-q` for quick tests. `./test-xv6.py crash` runs crash-recovery tests. The first argument is a regex over the script's `test_*` functions.

## Coding Style & Naming Conventions

- `.editorconfig`: LF endings, final newline, 2-space C, 8-space `.S`, tabs in `Makefile`, 4-space elsewhere.
- `.dir-locals.el`: BSD C style; keep C free of tabs.
- Use lowercase `snake_case` for functions, variables, and `struct` tags; `UPPER_SNAKE_CASE` for macros and constants.
- Put the return type on its own line and use concise `//` or `/* */` comments.
- No formatter or linter is configured; the build uses `-Wall`, so resolve compiler warnings.

## Testing Guidelines

No unit-test framework exists; coverage lives in `user/usertests.c` and the QEMU harness `test-xv6.py`.

- Add `void foo(char *s)` to `user/usertests.c` and register it in `quicktests[]` or `slowtests[]`.
- Run one test from the xv6 shell with `usertests foo`.
- Add orchestrated or crash checks as `test_foo()` functions in `test-xv6.py`.
- Run `./test-xv6.py usertests` and `./test-xv6.py crash` before submitting.

## Adding New Code

- Kernel sources: add the file to `OBJS` in the `Makefile` and export new APIs in `kernel/defs.h`.
- User programs: create `user/foo.c` and add `$U/_foo` to `UPROGS`.
- Kernel limits and configuration constants live in `kernel/param.h`.

## Commit & Pull Request Guidelines

This checkout contains no `.git` metadata, so no conventions can be read from local history. If you version-control this project, keep commits small with imperative subjects such as `add lazy allocation test`, one logical change per commit. For pull requests, describe the behavior change, list the tests run, and link related issues; screenshots are unnecessary for this CLI and kernel project.
