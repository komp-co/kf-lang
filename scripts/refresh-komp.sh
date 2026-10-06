#!/usr/bin/env sh
# Rebuild .build/kflatc from the working tree, with .build/komp beside it, the
# komp that drives it.
#
#   scripts/refresh-komp.sh              # stage1: the seed compiles this tree
#   scripts/refresh-komp.sh --stage2     # one generation further; see below
#   scripts/refresh-komp.sh --keep-going # install even if the smoke test fails
#
# komp is not built here: it is komp-co/komp's, and .build/komp is the driver
# bootstrap/stage0.toml pins, which runs the kflatc beside it. `~/.local/bin/komp`
# may be a symlink to `.build/komp`, so this updates the `komp` on PATH. The
# new kflatc is moved into place only after it answers `kflatc version`, so a
# broken build leaves the previous one.
#
# Stage1 is this tree compiled by the seed: it behaves as the source says, but
# was built by the old compiler, so it contains the old emission. `--stage2`
# builds once more with a compiler that has the change; use it when testing
# codegen.
set -eu

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

CC="${CC:-cc}"
CFLAGS="${CFLAGS:--O2}"

stage2=0
smoke=1
for arg in "$@"; do
    case "$arg" in
        --stage2)     stage2=1 ;;
        --keep-going) smoke=0 ;;
        -h|--help)    sed -n '2,30p' "$0"; exit 0 ;;
        *)            echo "usage: $0 [--stage2] [--keep-going]" >&2; exit 2 ;;
    esac
done

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

# What is about to be built, so a stale binary is identifiable later.
rev="$(git rev-parse --short HEAD 2>/dev/null || echo 'unknown')"
if ! git diff --quiet 2>/dev/null || ! git diff --cached --quiet 2>/dev/null; then
    rev="$rev+dirty"
fi
branch="$(git rev-parse --abbrev-ref HEAD 2>/dev/null || echo '?')"
echo "==> refreshing .build/komp from $branch @ $rev"

echo "[1/3] $CC the seed -> seed compiler"
. "$ROOT/bootstrap/seed.sh"
seed_build "$WORK/s0" || exit 1

echo "[2/3] seed compiles compiler/kflatc -> stage1.c"
"$WORK/s0/komp0" "$ROOT/compiler/kflatc" "$WORK/stage1.c" > "$WORK/emit.log" 2>&1 || {
    echo "FAIL: the seed cannot build this tree." >&2
    echo "      Errors below are from the seed compiling your source:" >&2
    grep -E 'error' "$WORK/emit.log" | head -20 >&2
    exit 1
}

echo "[3/3] $CC stage1.c -> kflatc"
"$CC" $CFLAGS -w -o "$WORK/kflatc" "$WORK/stage1.c" -lm
cp "$WORK/s0/komp0" "$WORK/komp"

if [ "$stage2" -eq 1 ]; then
    echo "[+1/1] stage1 compiles compiler/kflatc -> stage2.c"
    KFLATC="$WORK/kflatc" "$WORK/komp" "$ROOT/compiler/kflatc" "$WORK/stage2.c" > "$WORK/emit2.log" 2>&1 || {
        echo "FAIL: stage1 cannot build this tree." >&2
        grep -E 'error' "$WORK/emit2.log" | head -20 >&2
        exit 1
    }
    "$CC" $CFLAGS -w -o "$WORK/kflatc" "$WORK/stage2.c" -lm
    if cmp -s "$WORK/stage1.c" "$WORK/stage2.c"; then
        echo "      stage1.c == stage2.c (this tree is at its fixpoint)"
    else
        echo "      stage1.c != stage2.c — expected while a codegen change settles."
    fi
fi

# A binary that cannot answer for itself does not go on PATH.
if [ "$smoke" -eq 1 ]; then
    if ! "$WORK/kflatc" version > "$WORK/version.log" 2>&1; then
        echo "FAIL: the new kflatc did not survive \`kflatc version\`; keeping the old one." >&2
        head -5 "$WORK/version.log" >&2
        echo "      Pass --keep-going to install it anyway." >&2
        exit 1
    fi
fi

mkdir -p .build
# Move rather than copy: replacing the inode leaves no window in which the
# file on PATH is half-written.
mv -f "$WORK/kflatc" .build/kflatc.new
mv -f .build/kflatc.new .build/kflatc
mv -f "$WORK/komp" .build/komp.new
mv -f .build/komp.new .build/komp

echo "==> .build/komp is $branch @ $rev"
"$ROOT/.build/kflatc" version 2>&1 | head -1 || true

# `bin/komp` is the path the editor docs and the LSP README name; a symlink
# keeps it the same file as `.build/komp`.
mkdir -p bin
ln -sfn "$ROOT/.build/komp" bin/komp
ln -sfn "$ROOT/.build/kflatc" bin/kflatc
echo "==> bin/komp and bin/kflatc -> .build/"

# The symlink is outside the repo, so report rather than create it.
link="$HOME/.local/bin/komp"
if [ -L "$link" ]; then
    target="$(readlink "$link")"
    case "$target" in
        "$ROOT/.build/komp") echo "==> $link -> this build" ;;
        *) echo "NOTE: $link points at $target, not $ROOT/.build/komp" ;;
    esac
else
    echo "NOTE: $link is not a symlink to this build. To put it on PATH:"
    echo "        ln -sf $ROOT/.build/komp $link"
fi
