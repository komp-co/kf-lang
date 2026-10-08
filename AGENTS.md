# Repository Guidelines

## Where the truth lives

- **The issue tracker** (Gitea, milestones + issues) sets direction and holds
  every piece of open work. There is no roadmap document; do not write one.
- **`docs/book/`** describes the language, libraries and CLI as they are.
- **This file, `CONTRIBUTING.md`, `README.md` and `bootstrap/README.md`**
  hold the working rules.

Nothing else under `docs/` is kept. Design reasoning goes in the PR body and
the issue, where it is archived and cannot go stale in the tree.

## Project Structure

kflatc is the self-hosted KFlat compiler. Its source workspace lives under
`compiler/`; repository-level assets stay at the root:

```
bootstrap/   — the pinned seed (`stage0.toml`) and the self-hosting script
compiler/    — compiler workspace (`kf.toml`, `kf-*` passes, `kflatc/`)
docs/book/   — the user-facing book
libs/        — KFlat language libraries (core, alloc, std), `testing`, which
               crates with tests name in `[dev-dependencies]`, and core-tests,
               which tests core from outside
scripts/     — check.sh and the ratchets CI runs
tests/       — black-box executable integration fixtures
tools/       — kf-fuzz and kf-reduce: the front-end fuzzer, and the reducer
               that shrinks what it finds
build.sh     — root-level compiler build entry point
```

The passes run in order `kf-parse` → `kf-assemble` → `kf-resolve` →
`kf-typecheck` → `kf-mono` → `kf-lower` → `kf-codegen`. `kf-core` holds the
shared AST and diagnostics, `kf-interface` the compiled crate metadata
(`.kfi`), `kf-driver` the compiler's entry points (one crate, `check`), and
`kf-query` the `serve` protocol and the editor answers it gives.
`kf-integration` holds the whole-project tests: it reads a fixture's crates
through `komp metadata` and builds it by running komp (`KOMP`, which `komp
test` sets). `kflatc`, the one binary, links kf-driver and kf-query and turns
one crate into C. Run by the names `kf-lint` and `kf-editor`, links to it the
toolchain installs, it is the linter (`kf-lint`'s crate) and the editor's
endpoint (`kf-editor`'s). komp, which drives it, is
komp-co/komp's: this repository uses the released komp `bootstrap/stage0.toml`
pins and never its source. What the two agree on (the files kflatc writes,
which files make a crate, the `serve` protocol) is kflatc's documented command
line; a change to it keeps the komps already released working. kflatc's
version is `kflat_version()` in kf-driver, which a project's `kflat = "..."`
pin names. A crate may only import its declared dependencies.

## Build, Test, and Development Commands

Every command takes a project directory — the one holding `kf.toml`. There is
no single-file mode.

| Command | Purpose |
|---|---|
| `sh bootstrap/build.sh` | Full build from the released seed via `cc` + fixpoint self-compile |
| `sh scripts/check.sh` | The quick gate before pushing: ratchets, formatting, lints, changed crates' tests |
| `sh scripts/check.sh --full` | Everything CI runs, to reproduce a red job |
| `komp test compiler/<crate>` | Run one crate's `@test` functions |
| `komp build <dir>` | Compile to C and link, artifacts under `target/kflat` |
| `komp run <dir>` | Build and execute |
| `komp check <dir>` | Type-check without codegen; exit code for CI |

## Coding Style & Naming

- **Types:** `UpperCamelCase` — `Expression`, `TypeKind`
- **Functions & variables:** `lower_snake_case` — `parse_expr`, `check_stmt`
- **Constants:** `UPPER_SNAKE_CASE`; **files:** `lower_snake_case`
- **No abbreviations** except universal ones (`ctx` ok; `tc` is not)
- **No emojis** in code or commits
- **One concept per `.kf` file**: if you cannot describe the file in one
  sentence without "and", it is two files. No grab-bags (`utils.kf`,
  `helpers.kf`, `misc.kf`) — put a helper next to what uses it.
- **A module is the directory, not the file.** Splitting a file inside its
  directory changes no import; adding a sub-directory creates a new module and
  touches every importer.
- **A function belongs to its type.** One whose subject is a type its crate
  defines is a method in an `impl`, and a constructor is `static fun new`
  (`new_poison`, `new_aliased` for the others). Extension functions are for
  types another crate owns. The receiver is not repeated in the name:
  `TypeRef.is_borrow`, not `type_ref_is_borrow`.
- **A family with a subject but no value is a struct of statics.** The C
  identifiers the compiler emits are `CName.local`, `CName.field`; the mangle
  fragments are `Frag.borrow`, `Frag.function`. A free `pub fun` is the
  exception, for a word the whole compiler speaks (`intern`).
- **A fixed set of words is a constant.** `val RUNTIME_TYPE_NAMES: str[]`
  and `contains`, not a `when` with one `=> true` per word.
- **Declare no wider than the use.** `scripts/visibility.py` lists what
  could be `internal` or private; narrow it, and let `komp lint` object.
- **Text is text, not numbers.** A byte is compared with a char literal
  (`b == '_' as uint8`), and a search is the library call that names it
  (`contains`, `index_of`, `split`, `ends_with`) rather than a byte loop.
- **A crate's `README.md` is its map**: its modules and the rules that hold
  across them. A change to the modules updates it.
- **File length ratchets on growth.** `scripts/check_file_sizes.sh` fails when
  a file grows past its recorded size, or crosses 350 lines without a baseline
  entry. Imports and comment-only lines are not counted. Split it, or bless it
  with `--update` and say why in the commit.
- **Long lines ratchet too.** `scripts/check_line_lengths.sh` freezes the
  count of lines over 120 columns per file. Wrap instead: a trailing binary
  operator continues a line, and parameter and argument lists may span lines.
- **Unsafe ratchets down.** `scripts/check_unsafe.sh` freezes the count of
  `unsafe {` blocks per compiler source file. Own a node with
  `Box<T>`, borrow with `&T` or `&var T`, and call the library rather than
  declaring an `extern`; bless a block that must stay with `--update` and say
  why in the commit.
- **The tree is formatted.** CI runs `komp fmt --check compiler libs` with
  the formatter from the package index (`komp tool install komp_fmt`); run
  `komp fmt` on what you touch before committing. The `pre-commit` hook in
  `.githooks` checks the staged files when the formatter is installed.
- **Lints are errors in CI.** The sweep runs `komp lint --deny-warnings` on
  every crate. Fix what it reports; when a finding must stay, put
  `@allow(<lint>)` on the declaration and say why in the commit. The seed
  refuses an `@allow` naming a lint it predates, so for a lint newer than
  `bootstrap/stage0.toml`'s release, set it in the crate's `lint.toml`.
- **Output is deterministic.** The fixpoint is a byte comparison, so nothing
  whose order depends on hashing or addresses may reach emitted C or a `.kfi`.
- **Replace, don't accrete.** No `parse_expr_v2` beside `parse_expr`, no
  TODO comments (file an issue), no helper until there is a third use.

### Comments

**Comment the code, not the decision that produced it.** The reasoning behind
a change goes in the commit message and the PR body. A comment is re-read
every time the file is opened, and it rots.

Write only:

| Keep | Example |
|---|---|
| the contract a caller needs | "Indexing yields a borrow, never a copy" |
| a constraint, marked `CONSTRAINT:` | "runs after alpha_rename, so a plain name comparison is exact" |
| a mechanism the code cannot state | a layout diagram, a table of what arrives at a `when` |

Delete: history ("it used to", "before this", "no longer"), issue numbers and
issue archaeology, design alternatives, comparisons to other languages,
references to documents, and anything the code plainly says.

**Length is the smell.** Most files need no header at all; a header past ~5
lines, or a function comment past ~3, is nearly always a decision being
narrated. If it needs a paragraph, it belongs in the commit message.

### Kotlin style

The compiler was written before the language had most of its conveniences.
Write new code, and code you touch, the way the language reads now:

| Write | Rather than |
|---|---|
| `while d in &decls { ... }` | `var i = 0` / `while i < decls.size()` / `i = i + 1` |
| `while i in 0..n { ... }` when the index matters | the same, by hand |
| `val kind = if c { a } else { b }` | `var kind = b` then `if c { kind = a }` |
| `val g = find(xs, "g") ?: return 1` | a `when` whose `None` arm returns |
| `find(xs, "g")?.arity ?: -1` | nested `when`s over each optional |
| `fun Decl.is_nullary(): bool` | `fun is_nullary(d: &Decl): bool` |
| `"${name}/${arity}"` | a chain of `append` calls |
| `val` | `var` that is never reassigned |

Keep `append` for building a string in a loop, where `+` would copy the whole
accumulator each time. The List adapters (`map`, `filter`, `any`, `count`,
`fold`, `position`) are eager and hand each element to the lambda by value, so
over owning elements they copy; prefer a borrowing loop there. `let`, `also`,
`apply` and `take_if` are for removing a temporary, not for decoration.

## Testing

Tests use `@test` and live as sibling `_test.kf` files sharing scope with source:

```kf
@test
fun int32_ty_is_not_poison(): void {
    val t = int32_ty(no_span())
    assert_eq(t.is_poison(), false, "int32 isn't poison")
}
```

- New functions with branching logic ship with tests in the same commit
- Bug fixes include a regression test; run `komp test` on the crate before
  every commit, and `sh scripts/check.sh` (the quick gate) before pushing;
  CI runs the full gate, so do not run `--full` as well unless a job is red
- A change that breaks an existing test fixes the test or the change in the
  same commit — never leave the suite red
- One assertion per test; split unrelated assertions into named tests
- End-to-end behaviour goes in `tests/cases/*.kf` as a directive fixture
- A fuzz finding is fixed with its reduced program as a fixture, or filed and
  named in `tools/kf-fuzz/known.txt`; CONTRIBUTING.md has the steps
- `tools/` drives kflatc and komp as programs and imports no compiler crate

### Verifying a change

**`komp check` passing does not mean the program builds.** A good number of
open bugs pass the checker and fail in cc or at link, naming a mangled symbol
nobody wrote. Confirm with `komp run`, not `komp check`.

Put a scratch compiler in `bin/` — it is gitignored, and the stdlib sysroot
resolves through `<binary>/../libs`. A binary built anywhere else only works
when the working directory happens to be the repository root.

**Only komp reads a manifest.** The build graph (`effective_deps`) is the one
place a crate's dependencies are worked out, for every command. kflatc is
handed what it needs on its command line (`--crate`, `--dep`, `--lint`); do
not give it a reason to open `kf.toml`.

**Only `sync_sources` fetches.** It runs before a command builds, reads package
indexes, fetches dependencies into the cache and writes `kf.lock`. The build graph
reads the lock and the cache and nothing else, so it never touches the network.
Tests fetch from `file://` sources in scratch space, never from a real host.

## Commit Guidelines

- **Subject under 70 chars**, body wraps at 72; **name the *why*, not what**
- **One concept per commit** — split if the subject says "and" or "also"
- **Every commit passes CI** — tests green at every history point
- **Area prefix** in subjects: `parse:`, `typecheck:`, `codegen:`, `docs:`, `mono:`, `lower:`

## Documentation

**A change that alters observable behaviour updates the book in the same
commit**, so the tree never contradicts itself at any point in history.

| You changed | Update |
|---|---|
| syntax, semantics, or a diagnostic's text | `docs/book/src/lang/` |
| the library surface (`libs/`) | `docs/book/src/libs/` |
| kflatc's command line or the `serve` protocol | `docs/book/src/tools/serve.md`, and komp-co/komp's `tools/cli.md` if komp sees it |
| a rule other code must follow | this file |
| a keyword, an operator, or a builtin type name | regenerate the grammar in komp-co/kf-extensions (`vscode/scripts/generate-grammar.js`) |

**Fixing a bug the book documents as a limitation? Delete the entry.** Every
gap in `docs/book/src/limitations.md` names its issue; when the issue closes,
the entry and every in-chapter warning pointing at it go with it.

**Every book example is compiled before it is written down**, with a komp
built from the tree being changed. `docs/book/AUTHORING.md` has the method.

## Git and Forge

The repository is `komp-co/kf-lang` on GitHub, with `komp` (the project
tool), `json`, `komp-test`, `kf-lsp` and `kf-extensions` beside it in the same
organization. Use the `gh` CLI:

| Command | Purpose |
|---|---|
| `gh issue create --repo komp-co/kf-lang ...` | Create an issue |
| `gh pr create --repo komp-co/kf-lang --base development ...` | Open a pull request |
| `gh pr checks <n> --repo komp-co/kf-lang` | Watch a PR's CI |
| `gh pr merge <n> --repo komp-co/kf-lang` | Merge once CI is green |

Work merges into `development`. A PR from `development` into `main` is a
release; CONTRIBUTING.md has the steps.
