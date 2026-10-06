# testing

`testing` is the crate tests are written against. It depends only on `core`,
and komp adds it to a crate whenever that crate's `_test.kf` files are
compiled: by `komp test`, `komp check`, `komp lint` and the editor. A normal
build does not see it. A program that runs tests itself names it as a
dependency like any other crate:

```toml
[dependencies]
testing = { path = "../libs/testing" }
```

`core` cannot depend on `testing`, which depends on it, so core's own tests
live in a crate of their own, `libs/core-tests`, and test core through its
public API.

## Running tests

`testing` declares [`@test` and `@disabled`](../lang/annotations.md#test), and
`run_tests` is the entry point of a test program: `komp test` builds the main
`return run_tests(&annotated<test>(), &annotated<disabled>())`.

```kflat
import testing.disabled
import testing.run_tests
import testing.test

@test(name = "one and one make two")
fun adds(): void { assert_eq(1 + 1, 2, "one and one") }

fun main(): int32 {
    return run_tests(&annotated<test>(), &annotated<disabled>())
}
```

Each test runs in its own child process, so a panic fails that test and the
run goes on; a disabled test is reported as ignored. The exit code is 1 when a
test failed. The program answers three flags:

| Flag | Does |
|---|---|
| `--filter <substring>` | runs the tests whose name, or shown name, contains it; fails when none does |
| `--run-test <name>` | runs one test in this process |
| `--list` | prints each test as one JSON line and runs nothing |

A `--list` line names the function, the name shown, its module, its file and
line, the fault kind it expects (`""` for none, `"any"` for any), and the
reason it is disabled, or `null`:

```json
{"name":"adds","shown":"one and one make two","module":"app","file":"src/lib_test.kf","line":4,"panics":"","disabled":null}
```
