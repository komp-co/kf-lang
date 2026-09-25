# Projects and kf.toml

Every KFlat program lives inside a project directory. The only mandatory file
is `kf.toml` — it tells komp the crate name, version, and what kind of output
to produce.

## Creating a project

```console
$ komp new my-project
$ ls my-project
kf.toml  src/

$ komp init   # in an existing directory — scaffolds it in place
```

Both write the same two files:

### kf.toml

```toml
[project]
name = "my_project"
version = "0.1.0"
kind = "bin"
```

The name is derived from the directory: hyphens become underscores (`my-project`
→ `my_project`), because the crate name becomes a C identifier.

### kind: library vs binary

| `kind` | Entry point | Output |
|---|---|---|
| `"bin"` | Must have `fun main()` in the crate | Executable binary |
| `"lib"` | No main required | Shared or static library |

An executable needs `fun main(): void` (or `int32`). A library crate has no
entry point and can only be used as a dependency.

### src/main.kf

The generated template for a binary crate:

```kflat
fun main(): int32 {
    println("Hello, world!")
    return 0
}
```

## The src/ layout

Every `.kf` file under `src/` is part of the crate. A subdirectory becomes a
module:

```
src/
  main.kf          # crate root
  data.kf          # accessible as `data` from main.kf
  util/
    format.kf      # accessible as `util.format` from main.kf
```

A file `src/util/format.kf` is the module `util.format`. You import it like
this:

```kflat
import util.format.*
```

Imports are always `import`, never `use`. They appear before any declarations.

## Dependencies

To depend on another crate, add a `[dependencies]` section with a path:

```toml
[project]
name = "app"
version = "0.1.0"
kind = "bin"

[dependencies]
my_lib = { path = "../my_lib" }
```

The key (left of `=`) is how the dependency is imported in KFlat source:

```kflat
import my_lib.*
```

The path is relative to the `kf.toml` that declares it. Only local-path
dependencies are supported.

`core` and `alloc` do not need an entry — the compiler injects them into any
crate that declares no dependencies of its own, and a crate that *does* have
dependencies inherits theirs. `std` is the exception: it is never implicit,
so a project that uses it must name it. See [std](../libs/std.md).

## Workspaces

Several crates that are developed together can share one workspace: a
`kf.toml` with a `[workspace]` section instead of `[project]`, in the directory
above them.

```toml
[workspace]
members = ["app", "geometry"]
default-member = "app"
```

`members` lists the crate directories, relative to the workspace. A member
depends on another through an ordinary path dependency:

```toml
[project]
name = "app"
kind = "bin"

[dependencies]
geometry = { path = "../geometry" }
```

A command run at the workspace root works on `default-member`. `--package`
(or `-p`) picks another member, and `--workspace` picks every member.
`--package` accepts the crate name or the member's directory name, so
`-p kf-core` and `-p kf_core` both work. The flags also work from inside a
member, and a command run from a subdirectory such as `app/src` finds the
nearest `kf.toml` above it.

```console
$ komp run
$ echo $?
6
$ komp check -p geometry
check: OK
$ komp build --workspace
==> komp build ./app
==> komp build ./geometry
```

`--workspace` runs on every member even when one fails, and exits non-zero if
any did. `komp run` and whole-program builds (`--unity`, `-o`) work on a single
crate, so they accept `--package` but not `--workspace`. A workspace without
`default-member` does not guess:

```console
$ komp build
error: workspace `/home/me/shapes` has no default-member: add one, or pass `--package <name>` or `--workspace`
```

Members build into one shared `target/kflat` next to the workspace manifest, so
a dependency used by several members is compiled once. The members' own
directories get no `target`. Artifacts are named per crate, so members do not
overwrite each other. `target-dir` moves the shared directory; the path is
relative to the workspace:

```toml
[workspace]
members = ["app", "geometry"]
default-member = "app"
target-dir = "../build"
```

A crate belongs to the workspace only if `members` lists it. A crate that just
sits in a subdirectory keeps its own `target`.

Several komp processes can build into the same target at the same time, for
example an editor's `komp check` while a build runs in a terminal. komp writes
each artifact under a temporary name and renames it into place, so a reader
never sees a half-written file.

## Native C sources

If a crate ships C alongside KFlat, list the sources under `[native]`:

```toml
[native]
c_sources = ["src/wrapper.c", "src/helper.c"]
```

These are compiled and linked into the final binary. Any KFlat function
declared `extern "C"` can call them and be called by them. See
[unsafe, extern, and C interop](../lang/unsafe.md).

### The freestanding tier

Some of a crate's C only works where there is a C library behind it — a
console to print to, a heap to allocate from, a process that can fork.
Listing it separately says so:

```toml
[native]
c_sources        = ["native/core.c"]
hosted_c_sources = ["native/core_hosted.c"]
```

Both halves compile in an ordinary build. A program that is going somewhere
with no operating system says so in its own manifest:

```toml
[project]
name = "blinky"
kind = "bin"
freestanding = true
```

and then **no** crate's `hosted_c_sources` is compiled — not the program's,
and not any dependency's. That last part is the point of putting the flag on
the program rather than on each crate: `core` cannot know whether the thing
linking it has a `stdout`, so the program has to be the one that answers.

What komp emits is already freestanding. It includes `<stdint.h>`,
`<stdbool.h>` and `<stddef.h>` — the three C guarantees a freestanding
implementation provides — and routes every allocation through the
[allocation seam](../libs/core.md). So a freestanding build of a program
compiles with no C library present at all:

```console
$ komp blinky blinky.c
$ cc -c -ffreestanding -nostdinc -isystem "$(cc -print-file-name=include)" blinky.c
```

Linking is where the program has to answer for itself. The symbols the
hosted half would have defined are now undefined, and the linker names any
that were left out:

| symbol | what the program has to say |
|---|---|
| `kf_try_alloc`, `kf_try_realloc`, `kf_free` | where memory comes from |
| `panic` | how this target stops |
| `runtime_print`, `runtime_println` | where bytes go, if anywhere |

That is the intended diagnostic. The alternative is a program that builds
and then fails on a target that never had a `stdout` to begin with.

Freestanding is a build profile, so artifacts built one way are never reused
for the other — `libcore.a` compiled for a hosted program is a different
archive from the same source compiled for a freestanding one.
