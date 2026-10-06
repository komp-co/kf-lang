<img src="https://raw.githubusercontent.com/komp-co/kf-extensions/main/brand/kiwi.svg" width="96" alt="The KFlat paper kiwi">

# KFlat

A systems programming language that compiles to C, and `komp`, its
self-hosted compiler — written in KFlat, compiling itself to a byte-identical
fixpoint since 2026-06-14.

```kflat
fun main(): void {
    println("Hello, world!")
}
```

Apart from a small C shim for bootstrap and platform calls, the language needs
no runtime. Generics are monomorphized to ordinary functions at compile time,
dispatch is static unless you write a trait object, and there is no garbage
collector. If a program allocates, it is because it asked to.

KFlat is **not** memory-safe in Rust's sense: it has a move checker and
second-class borrows, but borrows are not lifetime-checked. A use-after-free
through a borrow compiles, and crashes. That model is being tightened —
[`docs/book/src/lang/memory.md`](docs/book/src/lang/memory.md) states what is
checked today and what is not.

## Build it

The only prerequisite is a C compiler. kflatc is built from a seed, the C
translation of a released kflatc, and the build is driven by a released komp;
the script fetches both once and checks them against the hashes in
`bootstrap/stage0.toml`:

```sh
KOMP_PUBLISH=out/komp sh bootstrap/build.sh   # out/kflatc, and the komp that drove it
```

It then checks the fixpoint — that kflatc compiled by kflatc reproduces itself
byte for byte. That single test guards the whole compiler.

## Use it

```sh
komp new hello        # scaffold a project
komp run hello        # build and run
komp check hello      # type-check only
komp test hello       # run its @test functions
```

`komp build` writes per-crate artifacts and links them; `--unity` builds
through a single C file instead. `komp check --fix` applies the repairs the
checker suggests. Every command takes a project directory — one containing a
`kf.toml` — or `--manifest-path`.

## Repository layout

| | |
|---|---|
| `compiler/` | komp itself, one crate per pass |
| `libs/` | `core` (no-std vocabulary), `alloc` (containers), `std` (hosted) |
| `bootstrap/` | the pinned seed and the bootstrap script |
| `tests/cases/` | end-to-end fixtures, each declaring the exit code it expects |
| `scripts/` | `check.sh` and the gates CI runs |
| `docs/book/` | the language book |

The compiler crates run in pipeline order: `kf-parse` → `kf-assemble` →
`kf-resolve` → `kf-typecheck` → `kf-mono` → `kf-lower` → `kf-codegen`, with
`kf-core` holding the shared AST and diagnostics, `kf-interface` the compiled
crate metadata that makes separate compilation work, and `kf-driver` the
compiler's entry points: compiling one crate, `check`, `query` and test mains.
`kf-tool` is the project tool: manifests, fetching, the build graph and `cc`.
It links none of the compiler crates; komp runs kflatc as a process.
komp and kflatc are versioned separately; a project's `kflat = "..."` pin
names kflatc's. `kf-integration` holds the compiler's whole-project tests, which run komp
as a program; komp's own are in kf-tool.

## Contributing

Run `scripts/check.sh` before pushing: the quick gate, with the ratchets,
formatting, lints, and the tests of the crates your branch changes and of
those that depend on them. CI runs everything else, and `--full` runs that
locally, to reproduce a red CI job.

```sh
scripts/check.sh              # the quick gate, before pushing
scripts/check.sh --full       # everything CI runs
scripts/check.sh --fixpoint   # the fixpoint alone
scripts/check.sh --sweep      # CLI checks and the crate sweep
```

The compiler depends on the [`json`](https://github.com/komp-co/json)
package from the index, at the version `compiler/kf.lock` pins. komp fetches
it on the first build, so that build needs the network.

Work is tracked in [issues](https://github.com/komp-co/komp/issues) and
milestones. The editor tooling lives in its own repositories:
[kf-extensions](https://github.com/komp-co/kf-extensions) (VS Code) and
[kf-lsp](https://github.com/komp-co/kf-lsp). [`CONTRIBUTING.md`](CONTRIBUTING.md) covers branches, CI and the
seed; [`AGENTS.md`](AGENTS.md) has the style, comment, test and commit rules.

## Documentation

[`docs/book/`](docs/book/) is for people using KFlat.
