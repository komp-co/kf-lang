# Installing komp

You need a C compiler (gcc or clang), and a network connection the first
time. komp is built from a **seed**: the C translation of a released komp and
of `kflatc`, the compiler it runs for each crate. Each release publishes one,
and `bootstrap/stage0.toml` pins the one this tree builds from.

## Building from the seed

```console
$ KOMP_PUBLISH=out/komp sh bootstrap/build.sh
[1/4] cc the seed, kflat 0.1.0 -> komp0, kflatc
[2/4] komp0 compiler/komp -> stage1.c ; cc stage1.c -> komp1
      komp0 compiler/kflatc -> kflatc1.c ; cc kflatc1.c -> kflatc
[3/4] komp1 compiler/komp -> stage2.c ; compiler/kflatc -> kflatc2.c
[4/4] fixpoint check
OK: stage1.c == stage2.c and kflatc1.c == kflatc2.c (fixpoint holds)
OK: published komp1 and kflatc to out
```

`KOMP_PUBLISH` names where the verified `komp` goes, with `kflatc` beside it.
Keep the two together: komp looks for `kflatc` in its own directory, or
wherever `KFLATC` points.

The chain:

1. The seed is fetched once into `~/.cache/kflat/seeds`, checked against the
   sha256 in `bootstrap/stage0.toml`, and compiled → komp0 and its kflatc
2. komp0 builds `compiler/komp` and `compiler/kflatc` → stage1 C → komp1 and
   the kflatc beside it
3. komp1 builds both again → stage2 C
4. Assert stage 1 == stage 2 byte-for-byte (the fixpoint)

The fixpoint is the proof: a compiler that can reproduce its own C output
exactly is self-hosting. The seed is a past compiler's output — enough for a
first build, but not proof by itself.

With no network, download the seed tarball from the release
`bootstrap/stage0.toml` names and point `KFLAT_SEED` at it.

## Why C

The compiler emits C. C toolchains (gcc, clang) target every architecture:
x86, ARM, RISC-V, AVR, bare-metal kernels. A C backend buys platform coverage
that a custom code generator would take years to reach. The C output is not a
temporary step toward a native backend — it is the strategy.

## Putting komp on PATH

Put both binaries in the same directory on your `PATH`:

```console
$ cp out/komp out/kflatc ~/.local/bin/
```

Make sure `~/.local/bin` (or wherever you put them) is on your `PATH`.
