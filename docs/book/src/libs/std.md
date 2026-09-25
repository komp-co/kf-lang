# std

`std` provides filesystem, environment, input, process and time utilities. Its modules
are independent — import what you need.

## std is not implicit

`core` and `alloc` are injected automatically. **`std` is not** — a project
that imports it has to name it:

```toml
[project]
name = "my-project"
version = "0.1.0"
kind = "bin"

[dependencies]
std = { path = "/path/to/kflat/libs/std" }
```

`core` and `alloc` stay available: `std` declares them, and a crate inherits
its dependencies' stdlib tier. Without the `std` entry, every `std` name
reports as not found in scope, with nothing pointing at the manifest as the
cause.

## std.fs

Filesystem operations:

```kflat
import std.fs.*

fun main(): int32 {
    // Create a directory (and any missing parents)
    val created = create_dir_all(Path.new("a/b/c"))

    // Make a temporary directory, removed when `tmp` is dropped
    val tmp = TempDir.new("my-prefix")
    val workdir = tmp.path()
    return 0
}
```

| Function | Return | Notes |
|---|---|---|
| `create_dir_all(Path)` | `bool` | `true` on success; creates parents |
| `TempDir.new(str)` | `TempDir` | Drops on scope exit — deletes the directory |

`Path` is in `alloc.path`, not `std.fs`. Import it separately.

## std.env

One function, and that is the whole module:

| Function | Return |
|---|---|
| `current_dir()` | `Path` — the process's working directory |

```kflat
import std.env.*

fun main(): int32 {
    val dir = current_dir()
    println(dir.as_str())
    return 0
}
```

There is **no** way to read an environment variable yet ([#58]). The process's
own command-line arguments are in core, with no import: `arg_count()` and
`arg_at(i)`, which borrows the argument rather than copying it.

## std.io

Standard input, read a line or a number of bytes at a time. Both return
`null` once the input is exhausted:

```kflat
import std.io.*

fun main(): int32 {
    var lines = 0
    while true {
        val line = read_line() ?: break
        if line.byte_len() > 0 { lines = lines + 1 }
    }
    return lines
}
```

`read_bytes(n)` reads up to `n` bytes, and `at_end()` says whether the last
read reached the end of input.

## std.process

Running a command:

```kflat
import std.process.*

fun main(): int32 {
    var cmd = Command.new("echo")
    cmd.arg("hello")
    cmd.arg("world")
    val result = cmd.status()
    return result.exit_code()
}
```

| Method | Returns |
|---|---|
| `Command.new(program: str)` | `Command` |
| `.arg(value: str)` | `void` — `mutating`, appends in place |
| `.current_dir(path: Path)` | `void` — `mutating` |
| `.env(key: str, value: str)` | `void` — `mutating`, sets it for the child |
| `.stdout_to(path: Path)` | `void` — `mutating`, truncates and redirects |
| `.status()` | `ExitStatus` |
| `.exit_code()` on `ExitStatus` | `int32` |
| `.success()` on `ExitStatus` | `bool` |

`Command` is **not** a builder. Every configuration method mutates in place
and returns `void`, so the receiver has to be a `var` and the calls have to be
separate statements. Chaining them — `Command.new("echo").arg("hello")` —
is rejected by `komp check`: `cannot call method `status` on `void``.

Arguments never pass through a shell, so no quoting or escaping is involved.
`status()` runs the command synchronously and blocks until the child exits.

## std.time

Reading the clock. The arithmetic — `Duration`, `Date`, `DateTime` — is
[`core`](core.md); this is the part that needs an operating system.

```kflat
import std.time.*

val t = now()               // current UTC DateTime
println(t)                  // "2026-08-21T11:54:56Z"
unix_seconds()              // seconds since the epoch

val started = Instant.now()
sleep(Duration.from_millis(50))
println(started.elapsed())  // "50ms"
```

**Two clocks, and they are not interchangeable.** `now()` reads the wall
clock: it names an instant everyone agrees on, and it can jump when the
machine is corrected. `Instant` reads the monotonic clock, which only ever
moves forward from an unspecified origin.

Use `Instant` for *how long*, and `now()` for *when*. Measuring a duration
against the wall clock is the classic bug — an NTP correction part-way
through yields a negative elapsed time. `Instant` therefore has no conversion
to a `DateTime`, and `elapsed()` cannot be negative.

`sleep` waits at least as long as asked, resuming if a signal interrupts it.
A non-positive duration returns immediately.

## What is missing

`std` is thin — the modules above are all of it. Not available ([#58]):

- Reading and writing file contents (there is a `read_file`/`write_file`
  extern but no `std.fs` wrapper yet), or anything that reports an IO failure
- Environment variables
- Writing to standard error
- Directory listing/walking
- Networking of any kind
- Threading or synchronization

The library surface grows as the language matures.

[#58]: https://github.com/komp-co/komp/issues/58
