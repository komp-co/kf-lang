# Contributing to kf-lang

## Where work is tracked

- **Issues and milestones** hold all open work and set the direction. There
  is no roadmap document in the tree.
- **Design reasoning** goes in the issue and the PR body, not in a file.
- **`docs/book/`** is the user-facing book — what the language, libraries and
  CLI do today.
- **`AGENTS.md`** holds the house rules: style, comments, tests, commits.

## Documentation lands with the change

A PR that alters observable behaviour and leaves the docs describing the old
behaviour is incomplete, and reviewers should say so. Keep the doc edit in the
same commit as the change it describes — a tree that contradicts itself at
some commit is a tree nobody can bisect against.

Concretely:

- **Language or library change** → the matching chapter under
  `docs/book/src/`. Compile the example before you write it down;
  `docs/book/AUTHORING.md` explains why that is the one hard rule.
- **Fixing something `limitations.md` documents** → delete the entry *and*
  the in-chapter warnings that link to the same issue. A stale limitation
  costs more than a missing one: it tells readers to avoid something that
  works.
- **New rule for other code** → `AGENTS.md`.

Docs-only PRs are welcome on their own; this rule is about not *falling
behind*, not about batching.

## Branch + PR convention

- **One branch per issue**, named `<type>/<issue#>-<slug>`:
  - `type` ∈ `feat`, `fix`, `hardening`, `perf`, `docs`, `chore`
  - examples: `fix/5-unresolved-dep-error`, `hardening/1-token-peek-borrow`
- Reference the issue in commits; close it from the PR with `Closes #<n>`.
- **PRs target `development`**, the default branch. Nothing is committed to
  `development` or `main` directly.

## Releases: `development` into `main`

`main` holds only released states. A merge into it **is** a release, and the
only step a person takes:

1. **Prepare release**, from the Actions tab, with the part of the version to
   raise. It opens a PR raising `kflat_version()` on `development`, which
   merges itself once CI is green.
2. That merge opens the release PR from `development` into `main`
   (`open-release.yml`). The `release-pr` check refuses any other source
   branch, and a version that does not go up.
3. **Merge the release PR.** The release workflow builds the seed from the
   pinned one, checks the fixpoint, publishes `vX.Y.Z` with
   `kflat-seed-X.Y.Z.tar.gz` and the install archive `kflat-X.Y.Z.tar.gz`,
   and opens the PR pinning that seed in
   `bootstrap/stage0.toml`, which merges itself once CI bootstraps from it.

The automated PRs are pushed and opened with the organization secret
`RELEASE_TOKEN`, a fine-grained token with Contents and Pull requests write on
the komp-co repositories, because a PR opened with a workflow's own token runs
no CI. When it expires, releases still publish, and the workflows say which
step was left to do by hand. Auto-merge must be allowed on the repository.
`scripts/release-version.sh` is how every workflow reads and raises the
version. Each step can also be done by hand, as an ordinary PR.

Both branches are protected **server-side**: GitHub refuses a direct push, and
a merge needs green CI, so every change lands via PR whatever your local setup
does.

A `pre-push` hook mirrors that rule locally, so the refusal arrives before the
round trip rather than after it. Enable it once per clone:

```sh
git config core.hooksPath .githooks
```

`git push --no-verify` bypasses the *local* hook only — the server-side rule
still stands, which is the point of having both.

The same setting enables a `pre-commit` hook that refuses a commit whose
staged `.kf` files `komp fmt` would change, or whose crates `komp lint
--deny-warnings` rejects: `scripts/preflight.sh --staged`. Formatting needs
the formatter (`komp tool install komp_fmt`); the lints use `KOMP`, else a
scratch `bin/komp`, else `komp` on the PATH, and are skipped with a note when
there is none. CI makes the same checks over every crate.

## CI

`.github/workflows/ci.yml` runs on every pull request — whatever it targets, so
a stacked PR is covered before its base merges — on every push to `main`, and
once a night on `development`.

A `classify` job first decides what the PR needs (`scripts/ci-classify.sh`):

| The PR changes | Jobs that run |
|---|---|
| only Markdown | none |
| only Markdown and `//` comment lines in `.kf` files | fixpoint (includes the ratchets: file size, line length, unsafe blocks) |
| anything else | all five |

The compiler's [`json`](https://github.com/komp-co/json) dependency comes
from the index at the version `compiler/kf.lock` pins, so a change to `json`
reaches komp only through a PR that moves the lock (`komp update compiler`).
The five jobs are:

1. **fixpoint** — `sh scripts/check.sh --fixpoint`, the self-host fixpoint
   (stage1 == stage2),
2. **cli** — the CLI checks from `scripts/check.sh --sweep`, with the
   sanitizer probes,
3. **sweep-driver** — the `kf-integration` test suite, the slowest crate,
4. **sweep-rest** — every other crate's test suite,
5. **fuzz** — the [front-end fuzzer](#fuzzing) on fixed seeds; at night, on
   fresh ones. It runs only in CI, beside the others.

The fixpoint job uploads the komp it verified, and the others wait for
it and reuse it rather than each building one from the seed. `cc` goes through
ccache, kept across runs with `actions/cache`.

The first four are required checks on `development` and `main`, and fuzz
should be too once branch protection names it; a job skipped by `classify`
counts as passed. Each CI run costs about ten minutes, so group related changes into one
PR rather than opening many small ones.

Before pushing, run the quick gate. It runs the ratchets, the formatting
check, the lints, and the tests of every crate your branch changes and of
every crate that depends on one of them, against the merge base with
`origin/development`:

```sh
sh scripts/check.sh
```

It leaves to CI what CI already runs in parallel: the fixpoint, the CLI
checks, the asan probes, `kf-integration` (the slowest crate, eleven minutes
alone) and the fuzzer. Do not run the full gate before every push as well;
CI is that run, on a clean checkout. When a CI job goes red, reproduce it with
the full gate, or just its half:

```sh
sh scripts/check.sh --full        # everything CI runs
sh scripts/check.sh --fixpoint    # the fixpoint job
CRATES="compiler/kf-integration" sh scripts/check.sh --sweep
komp test compiler/kf-integration --case <substring>   # some fixtures only
```

`sh scripts/preflight.sh` is quicker still, seconds: the ratchets, the
formatting of changed files, and the changed crates' lints.

The quick gate builds its komp from the seed, unless a fixpoint passed for
the same sources: that komp is cached under `~/.cache/kflat/gate`, keyed by
the sources it compiled with test files left out, and reused by both gates.
`KOMP_PREBUILT` names another; `KF_GATE_CACHE=0` turns the cache off.

It reports every crate rather than stopping at the first failure, so one red
run tells you everything that is broken instead of only the earliest thing.

Two ways to get a wrong answer out of it:

- **Do not build while it runs.** It compiles into shared dependency target
  directories, and a concurrent build corrupts the sweep. Let it
  finish — or run `sh scripts/check.sh --isolated`, which does the whole gate
  in a throwaway worktree and leaves this tree free to build and edit. That
  snapshot is HEAD plus your uncommitted tracked edits plus untracked files
  git would not ignore, so it gates what is on disk; anything in
  `.gitignore` is not carried across. CI does not use it and should not:
  every job already starts from its own checkout.
- **Do not pipe it into `tail`.** You get `tail`'s exit code, which is always
  0. Redirect to a log and check `$?`.

## Fuzzing

`tools/kf-fuzz` holds kflatc to one rule: whatever its input, it ends with
diagnostics. It mutates the `tests/cases` fixtures (deleted, duplicated and
swapped tokens, unbalanced delimiters, odd bytes, truncation) and, in its
`kfi` mode, the library interfaces they build against, then compiles each
mutant. A crash, a panic or a hang is a finding, named by a signature: the
panic's message, or the signal or hang and, when gdb is installed, the
function it struck in.

The `fuzz` job runs it through `scripts/check_fuzz.sh`: first every fixture
as written (`kf_fuzz corpus`), since a `build: fail` fixture also passes when
kflatc crashes, then mutants over fixed seeds. It fails on a signature
`tools/kf-fuzz/known.txt` does not name; the
nightly run gives it fresh seeds and uploads what it finds. It is not part
of `scripts/check.sh`; run the script yourself to fuzz a change locally:

```sh
sh scripts/check_fuzz.sh bin/komp
```

When the gate reports a finding, it prints the seed. Rebuild the program,
shrink it, and turn it into a fixture:

```sh
tools/kf-fuzz/target/kflat/kf_fuzz front --seeds 229..229 --emit /tmp/f
tools/kf-reduce/target/kflat/kf_reduce /tmp/f/front-seed229.kf -- \
    tools/kf-fuzz/target/kflat/kf_fuzz judge --expect "SIGNATURE"
```

`kf_fuzz judge` prints a program's signature, and with `--expect` exits 0
only on that one, which makes it the reducer's predicate. The reduced program
goes to `tests/cases` with the fix. A finding left for later gets an issue,
and its signature a line in `known.txt` naming it; the line goes when the
issue closes, so a recurrence fails the gate again.

## The bootstrap seed

The seed is a released kflatc as C, with the released komp that drives the
build. `bootstrap/stage0.toml` pins which release, and `bootstrap/build.sh` fetches and verifies it; nothing
generated is checked in. [`bootstrap/README.md`](bootstrap/README.md) has the
mechanics.

A stale seed is not a failure: `bootstrap/build.sh` reports it as a `NOTE`,
CI gates on the *fixpoint*, and the fixpoint holds regardless of how old the
seed is.

### When a change needs a newer seed

Sometimes the seed cannot build the tree: the change makes kflatc's own source
use a construct the seed mis-compiles or does not parse. `bootstrap/build.sh`
catches it and reports `FAIL: the seed cannot build this tree` (a C error in
stage1) or `FAIL: the seed rejected the current source` (a KFlat error).

Split the work so every commit on `main` bootstraps from the pinned seed:

1. **PR 1** — the compiler change, without using it in `compiler/`, into
   `development`.
2. **A release** — `development` into `main`, raising `kflat_version()`: the
   release workflow publishes a seed that knows the change.
3. **PR 2** — pin that release in `stage0.toml`, and the change that depends
   on it.

Before releasing, try the seed locally: `bootstrap/build.sh --seed-out DIR`
writes it, and `KFLAT_SEED=DIR/kflat-seed-X.Y.Z.tar.gz` bootstraps from it.

### A syntax feature reaches `compiler/` only after a release

The case above is the seed mis-*compiling* something, and it fails inside `cc`.
The other half fails earlier and reads worse: the seed cannot **parse** a
construct the tree has since made legal, so it reports an ordinary parse error
against your source and nothing says the compiler reading the file is older than
the file.

```
expression.kf:118:30: error: a lambda can only initialize a local or be passed
directly to a function call; ...
```

The actual cause was a *leading-operator continuation line*, which had just
become legal and which the seed predated:

```kflat
val opens_range: bool = is_op(p.peek(), Operator.DotDot)
                     || is_op(p.peek(), Operator.DotDotEq)
```

So: **every syntax feature has a window between landing and the release that
carries it, during which `libs/` and user code may use it and `compiler/` may
not.** If the seed rejects source you believe is correct, check that first —
the remedy is the two-PR recipe above, not a rewrite of the line it pointed at. The seed cannot
detect its own age, which is why this is written down rather than checked.

## Local dev quickstart

- The first build fetches `json` from the index.
- `sh scripts/refresh-komp.sh` builds `.build/kflatc` from the tree, with the
  pinned driver komp beside it as `.build/komp`.
- Self-host + fixpoint: `sh bootstrap/build.sh` (fetches the seed once).
- Test one crate: `komp test compiler/<crate>`.
