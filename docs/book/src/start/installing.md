# Installing komp

You need a C compiler (gcc or clang) and nothing else. The komp source tree
ships a checked-in C seed — a single file, `bootstrap/komp.c` — that can
compile the current tree. From that seed you build two binaries: `komp`, the
tool you run, and `kflatc`, the compiler it runs for each crate.

## Building from the seed

```console
$ cc -O2 -o komp0 bootstrap/komp.c
$ ./komp0 compiler/komp out/komp.c
$ cc -O2 -o out/komp out/komp.c
$ ./komp0 compiler/kflatc out/kflatc.c
$ cc -O2 -o out/kflatc out/kflatc.c
```

The seed compiles the KFlat source under `compiler/komp` and `compiler/kflatc`
to C, and then your C compiler turns that C into binaries. Keep the two
together: komp looks for `kflatc` in its own directory, or wherever `KFLATC`
points.

The full bootstrap chain that the automated script runs is one stage longer:

1. `cc bootstrap/komp.c` → komp0 (the seed binary)
2. komp0 builds `compiler/komp` → stage1.c; `cc stage1.c` → komp1
3. komp1 builds `compiler/komp` → stage2.c
4. Assert stage1.c == stage2.c byte-for-byte (the fixpoint)

The fixpoint is the proof: a compiler that can reproduce its own C output
exactly is self-hosting. The seed is a frozen snapshot of a past compiler's
output — enough for a first build, but not proof by itself.

To run the full chain:

```console
$ bash bootstrap/build.sh
[1/4] cc bootstrap/komp.c -> komp0
[2/4] komp0 compiler/komp -> stage1.c ; cc stage1.c -> komp1
[3/4] komp1 compiler/komp -> stage2.c
[4/4] fixpoint check
OK: stage1.c == stage2.c (fixpoint holds)
```

## Why C

The compiler emits C. C toolchains (gcc, clang) target every architecture:
x86, ARM, RISC-V, AVR, bare-metal kernels. A C backend buys platform coverage
that a custom code generator would take years to reach. The C output is not a
temporary step toward a native backend — it is the strategy.

## Putting komp on PATH

The bootstrap script leaves its binaries in a temp directory. To keep them,
put both in the same directory on your `PATH`:

```console
$ cp out/komp out/kflatc ~/.local/bin/
```

Make sure `~/.local/bin` (or wherever you put them) is on your `PATH`.
