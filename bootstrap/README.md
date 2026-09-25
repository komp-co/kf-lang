# Bootstrap

komp is written in KFlat, so building it needs a KFlat compiler. The **seed**
breaks that loop: a released komp and kflatc, translated to C, which any C
compiler can build.

```sh
sh bootstrap/build.sh
```

`stage0.toml` pins the seed: a release version, the URL of its
`kflat-seed-<version>.tar.gz`, and that file's sha256. `seed.sh` fetches it
once into `~/.cache/kflat/seeds` and verifies it on every use;
`KFLAT_SEED=<tarball>` uses a local one instead.

## How it works

```
cc the seed                     -> komp0, with its kflatc beside it
komp0 builds compiler/komp      -> stage1.c ; cc -> komp1
komp0 builds compiler/kflatc    -> kflatc1.c ; cc -> kflatc beside komp1
komp1 builds both again         -> stage2.c, kflatc2.c
assert stage1 == stage2         # the fixpoint
```

The fixpoint, byte for byte, is the single test that guards the whole
compiler: if komp can no longer reproduce itself, this fails.

## A new seed

The seed only has to *build* the current source, so it can lag. A new one
comes from a release: bump `kflat_version()`, merge, and push the tag
`vX.Y.Z`. The release workflow builds the seed from the pinned one, checks the
fixpoint, and publishes `kflat-seed-X.Y.Z.tar.gz` with its sha256. Pinning it
is then an ordinary change to `stage0.toml`.

To try a seed before releasing it:

```sh
sh bootstrap/build.sh --seed-out /tmp/seed
KFLAT_SEED=/tmp/seed/kflat-seed-X.Y.Z.tar.gz sh bootstrap/build.sh
```
