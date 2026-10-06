# Bootstrap

kflatc is written in KFlat, so building it needs a KFlat compiler. The **seed**
breaks that loop: a released kflatc, translated to C, which any C compiler can
build. A released komp drives the build; it only runs kflatc, so it can be any
komp that drives that release.

```sh
sh bootstrap/build.sh
```

`stage0.toml` pins the seed: a release version, the URL of its
`kflat-seed-<version>.tar.gz`, and that file's sha256. It pins the driver too,
as `komp_url` and `komp_sha256`, a released komp archive holding `bin/komp`;
a seed that ships its own `komp.c`, as the releases made before komp had a
repository of its own do, needs neither. `seed.sh` fetches both once into
`~/.cache/kflat/seeds` and verifies them on every use; `KFLAT_SEED=<tarball>`
uses a local seed instead.

## How it works

komp0, the driver, runs whichever kflatc `KFLATC` names; that kflatc emits
the C.

```
cc the seed                     -> kflatc0
kflatc0 builds compiler/kflatc  -> kflatc1.c ; cc -> kflatc1
kflatc1 builds compiler/kflatc  -> kflatc2.c
assert kflatc1.c == kflatc2.c   # the fixpoint
```

The fixpoint, byte for byte, is the single test that guards the whole
compiler: if kflatc can no longer reproduce itself, this fails.

kflatc1.c is the *seed's* output. A tree that changes what the compiler emits
for its own source cannot match it, so then the chain runs once more:

```
cc kflatc2.c                    -> kflatc2
kflatc2 builds compiler/kflatc  -> kflatc3.c
assert kflatc2.c == kflatc3.c   # the fixpoint
```

Either way the fixpoint is two compilers built from the same source agreeing,
and `--seed-out` packs the one that proved it: the seed, and the toolchain
archive `kflat-X.Y.Z.tar.gz`, which installs kflatc and its libraries.

## A new seed

The seed only has to *build* the current source, so it can lag. A new one
comes from a release: a PR from `development` into `main` that raises
`kflat_version()`. Merging it runs the release workflow, which builds the seed
from the pinned one, checks the fixpoint, and publishes
`kflat-seed-X.Y.Z.tar.gz` and `kflat-X.Y.Z.tar.gz` with their sha256 under
the tag `vX.Y.Z`, then opens
the PR that pins it in `stage0.toml`. CONTRIBUTING.md has the whole release.

To try a seed before releasing it:

```sh
sh bootstrap/build.sh --seed-out /tmp/seed
KFLAT_SEED=/tmp/seed/kflat-seed-X.Y.Z.tar.gz sh bootstrap/build.sh
```
