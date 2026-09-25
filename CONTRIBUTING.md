# Contributing to komp

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
- **Never commit directly to `main`.** All changes land via PR.

## main protection

`main` is protected **server-side**: GitHub refuses a direct push, so every
change lands via PR whatever your local setup does.

A `pre-push` hook mirrors that rule locally, so the refusal arrives before the
round trip rather than after it. Enable it once per clone:

```sh
git config core.hooksPath .githooks
```

`git push --no-verify` bypasses the *local* hook only — the server-side rule
still stands, which is the point of having both.

## CI

`.github/workflows/ci.yml` runs on every pull request — whatever it targets, so
a stacked PR is covered before its base merges — on every push to `main`, and
once a night.

A `classify` job first decides what the PR needs (`scripts/ci-classify.sh`):

| The PR changes | Jobs that run |
|---|---|
| only Markdown | none |
| only Markdown and `//` comment lines in `.kf` files | fixpoint (includes the size ratchets) |
| anything else | all four |

The four jobs each clone the public
[`json`](https://github.com/komp-co/json) dependency as a sibling
(`../json`):

1. **fixpoint** — `sh scripts/check.sh --fixpoint`, the self-host fixpoint
   (stage1 == stage2),
2. **cli** — the CLI checks from `scripts/check.sh --sweep`,
3. **sweep-driver** — the `kf-driver` test suite, the slowest crate,
4. **sweep-rest** — every other crate's test suite.

The fixpoint job uploads the komp it verified, and the other three wait for
it and reuse it rather than each building one from the seed. `cc` goes through
ccache, kept across runs with `actions/cache`.

All four are required checks on `main`; a job skipped by `classify` counts as
passed. Each CI run costs about ten minutes, so group related changes into one
PR rather than opening many small ones.

Run both gates locally before pushing — same checks, no round trip. It takes
minutes and grows with the tree, so time it rather than trusting a figure
quoted here:

```sh
sh scripts/check.sh
```

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

## The bootstrap seed

`bootstrap/komp.c` is the checked-in C seed: a whole compiler emitted as one
file of several megabytes, growing with the compiler it was cut from. It is
generated output, and it is the single biggest source of merge pain in this
repo — two branches that both regenerate it always conflict, while their
actual source changes merge fine.

**Do not refresh the seed on a feature branch.** A stale seed is not a failure:
`bootstrap/build.sh` reports it as a `NOTE`, CI gates on the *fixpoint*, and the
fixpoint holds regardless of how old the seed is. Leave the file alone and your
branch will merge cleanly.

Refresh it on `main`, in its own commit, whenever it has drifted:

```sh
sh bootstrap/build.sh --refresh   # verify, then install the seed it just proved
```

The bytes it installs are `stage1.c` from that same run — the seed's own output,
which stage 2 proved reproduces itself. Nothing needs rebuilding afterwards, so
resist doing it by hand: regenerating the seed separately costs several more
full compiles and proves nothing extra.

Keep that cadence *serial*: the problem is never an old seed, it is two new
ones in flight at once. How often is a judgement call. A refresh is its own PR
and its own CI round trip, and a seed that has drifted costs nothing but a
`NOTE`, so batching several merges into one refresh is usually the better
trade. What does force one is a change that needs the seed to understand a
construct it predates — see below.

`.gitattributes` marks the file `-diff -merge`, so it stays out of diffs and
never gets a textual 3-way merge. If you do hit a conflict on it, the resolution
is to **regenerate**, never to pick a side — see below for why neither side is
necessarily right.

### When a change forces a seed refresh

Sometimes the seed genuinely cannot build the tree: the change makes komp's own
dependencies use a construct the seed mis-compiles. The failure surfaces inside
the C compiler, several thousand lines into generated code; `bootstrap/build.sh`
catches it and reports `FAIL: the checked-in seed cannot build this tree`, which
is the signal for the recipe below.

The recipe is to split the work so every commit bootstraps from its predecessor:

1. **commit 1** — the compiler fix, plus a refreshed seed. Verify the *previous*
   seed can still build this commit.
2. **commit 2** — the change that depends on the fix. Verify **commit 1's seed**
   builds it.

Example: migrating `Iterable` to an associated type made `libs/alloc` bind one
per element type, which tripped a codegen defect the seed still had. Landing
both together would have left a branch whose seed could not build its own tree.

The same bind shows up when merging two long-lived branches — each side's seed
may be unable to build the other's source. Stage it the same way: build one
side's compiler, use it on the merged tree with the dependent change held back,
then use *that* compiler for the full tree.

### A syntax feature reaches `compiler/` only after a reseed

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

So: **every syntax feature has a window between landing and the next reseed
during which `libs/` and user code may use it and `compiler/` may not.** If the
seed rejects source you believe is correct, check that first — the remedy is the
two-commit recipe above, not a rewrite of the line it pointed at. The seed cannot
detect its own age, which is why this is written down rather than checked.

## Local dev quickstart

- The compiler needs the `json` crate cloned as a sibling (`../json`) and the
  checkout directory named lowercase `komp`: a path dependency reaches the
  stdlib through that name.
- Self-host + fixpoint: `sh bootstrap/build.sh`.
- Test one crate: `komp test compiler/<crate>`.
