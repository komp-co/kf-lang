#!/usr/bin/env sh
# Bootstrap komp from the checked-in C seed and verify the self-hosting
# fixpoint. Needs only a C compiler.
#
#   bootstrap/build.sh            # build + fixpoint check
#   bootstrap/build.sh --refresh  # ... and install stage1.c as the new seed
#   CC=clang bootstrap/build.sh   # pick a C compiler (default: cc)
#   CFLAGS=-O0 bootstrap/build.sh # cheaper cc, a far slower komp
#
# `-O0` makes cc quicker but komp far slower; even here, where komp runs
# twice, it loses.
#
# Chain:
#   1. cc bootstrap/komp.c            -> komp0   (the seed binary)
#   2. komp0 builds compiler/komp     -> stage1.c ; cc stage1.c -> komp1
#      komp0 builds compiler/kflatc   -> kflatc1.c ; cc -> kflatc, beside komp1
#   3. komp1 builds compiler/komp     -> stage2.c, running that kflatc
#      komp1 builds compiler/kflatc   -> kflatc2.c
#   4. assert stage1.c == stage2.c and kflatc1.c == kflatc2.c (the fixpoint)
#
# Each step fails for its own reason, named with its remedy.
#
# Exit non-zero if any stage or the fixpoint fails. Run this in CI.
set -eu

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
CC="${CC:-cc}"
CFLAGS="${CFLAGS:--O2}"

REFRESH=0
for arg in "$@"; do
    case "$arg" in
        --refresh) REFRESH=1 ;;
        *) echo "usage: $0 [--refresh]" >&2; exit 2 ;;
    esac
done

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

# fail HEADLINE [DETAIL...] — one FAIL: line, then indented remedy lines.
fail() {
    echo ""
    echo "FAIL: $1"
    shift
    for line in "$@"; do
        echo "  $line"
    done
    exit 1
}

# A tool killed by a signal did not reject anything. 137 is SIGKILL, on a
# build machine almost always the OOM killer: this script's peak is one whole
# self-compile.
#
# Call with the exit status of the tool that just failed; returns 0 when it
# named a signal, having already explained it.
report_if_killed() {
    status="$1"
    stage="$2"
    if [ "$status" -le 128 ]; then
        return 1
    fi
    echo ""
    echo "FAIL: $stage was killed by signal $((status - 128))."
    if [ "$status" -eq 137 ]; then
        echo "  SIGKILL, and nothing here sends it — this is the machine, not"
        echo "  your source. bootstrap/build.sh peaks at roughly one whole"
        echo "  self-compile; check free memory, and whether another build or"
        echo "  CI job was running at the same time."
    fi
    exit 1
}

# Each C build is a `-c` compile then a link: ccache caches only the former.
echo "[1/4] $CC bootstrap/komp.c -> komp0"
if ! "$CC" $CFLAGS -c -o "$WORK/komp0.o" bootstrap/komp.c || ! "$CC" $CFLAGS -o "$WORK/komp0" "$WORK/komp0.o"; then
    fail "the checked-in seed does not compile with $CC." \
         "bootstrap/komp.c is generated C that a working komp emitted, so this" \
         "is about the C toolchain, not about your source: either this" \
         "compiler is stricter than the one that last refreshed the seed, or" \
         "the seed was committed broken. Fix codegen, then refresh the seed" \
         "from a build made with an older/looser compiler."
fi

# Absolute project root: a seed older than the walk_project dedup fix
# (abs_path.kf) assembles shared crates twice when the root is relative.
echo "[2/4] komp0 compiler/komp -> stage1.c ; $CC stage1.c -> komp1"
stage1_status=0
"$WORK/komp0" "$ROOT/compiler/komp" "$WORK/stage1.c" || stage1_status=$?
if [ "$stage1_status" -ne 0 ]; then
    report_if_killed "$stage1_status" "the seed reading the current source" || true
    fail "the checked-in seed rejected the current source." \
         "The errors above come from komp0 — the seed binary — reading" \
         "compiler/komp. Either the source is genuinely broken, or it uses a" \
         "construct bootstrap/komp.c predates. Build with a current komp to" \
         "tell the two apart: if that succeeds, the seed is the problem." \
         "See \"The bootstrap seed\" in CONTRIBUTING.md."
fi
# From inside `$WORK`, so its random name stays out of the hashed command line.
if ! (cd "$WORK" && "$CC" $CFLAGS -c -o stage1.o stage1.c) || ! "$CC" $CFLAGS -o "$WORK/komp1" "$WORK/stage1.o"; then
    fail "the checked-in seed cannot build this tree." \
         "The errors above are in stage1.c — the seed's *output*, not your" \
         "source. bootstrap/komp.c miscompiles something this tree now depends" \
         "on, typically a codegen defect fixed after the seed was last cut." \
         "Land the codegen fix plus a refreshed seed as an earlier commit, then" \
         "rebase this change onto it, so every commit bootstraps from its" \
         "predecessor. See \"The bootstrap seed\" in CONTRIBUTING.md."
fi

# komp1 compiles through the kflatc beside it, which the seed builds here.
echo "      komp0 compiler/kflatc -> kflatc1.c ; $CC kflatc1.c -> kflatc"
kflatc1_status=0
"$WORK/komp0" "$ROOT/compiler/kflatc" "$WORK/kflatc1.c" || kflatc1_status=$?
if [ "$kflatc1_status" -ne 0 ]; then
    report_if_killed "$kflatc1_status" "the seed reading compiler/kflatc" || true
    fail "the checked-in seed rejected compiler/kflatc." \
         "It accepted compiler/komp, which holds the same compiler, so look at" \
         "compiler/kflatc itself."
fi
if ! (cd "$WORK" && "$CC" $CFLAGS -c -o kflatc1.o kflatc1.c) || ! "$CC" $CFLAGS -o "$WORK/kflatc" "$WORK/kflatc1.o"; then
    fail "the checked-in seed cannot build kflatc; see the stage1.c advice above."
fi

echo "[3/4] komp1 compiler/komp -> stage2.c ; compiler/kflatc -> kflatc2.c"
stage2_status=0
"$WORK/komp1" "$ROOT/compiler/komp" "$WORK/stage2.c" || stage2_status=$?
if [ "$stage2_status" -ne 0 ]; then
    report_if_killed "$stage2_status" "the freshly built compiler reading this tree" || true
    fail "the compiler this tree builds cannot compile this tree." \
         "komp1 came from the current source and stage 2 proved the seed can" \
         "build it, so the seed is not implicated: this is a regression in the" \
         "tree itself. Refreshing bootstrap/komp.c would only bake it in."
fi

kflatc2_status=0
"$WORK/komp1" "$ROOT/compiler/kflatc" "$WORK/kflatc2.c" || kflatc2_status=$?
if [ "$kflatc2_status" -ne 0 ]; then
    report_if_killed "$kflatc2_status" "the freshly built compiler reading compiler/kflatc" || true
    fail "the compiler this tree builds cannot compile compiler/kflatc."
fi

echo "[4/4] fixpoint check"
if ! diff -q "$WORK/stage1.c" "$WORK/stage2.c" >/dev/null; then
    echo "FAIL: fixpoint broken (stage1.c != stage2.c)"
    echo "  the compiler does not reproduce itself; see diff:"
    diff "$WORK/stage1.c" "$WORK/stage2.c" | head -40
    exit 1
fi
if ! diff -q "$WORK/kflatc1.c" "$WORK/kflatc2.c" >/dev/null; then
    echo "FAIL: fixpoint broken (kflatc1.c != kflatc2.c)"
    diff "$WORK/kflatc1.c" "$WORK/kflatc2.c" | head -40
    exit 1
fi
echo "OK: stage1.c == stage2.c and kflatc1.c == kflatc2.c (fixpoint holds)"

# KOMP_PUBLISH names a path to copy the verified komp1 to, with the kflatc
# beside it, so other jobs can reuse both instead of rebuilding them from the
# seed. Each is written beside its target and renamed, so a reader sees the
# whole binary or none. kflatc1.c and kflatc2.c are equal by now, so the
# kflatc already built is the one the tree builds.
if [ -n "${KOMP_PUBLISH:-}" ]; then
    publish_dir="$(dirname "$KOMP_PUBLISH")"
    mkdir -p "$publish_dir"
    cp "$WORK/kflatc" "$publish_dir/kflatc.tmp.$$" && mv -f "$publish_dir/kflatc.tmp.$$" "$publish_dir/kflatc"
    cp "$WORK/komp1" "$KOMP_PUBLISH.tmp.$$" && mv -f "$KOMP_PUBLISH.tmp.$$" "$KOMP_PUBLISH"
    echo "OK: published komp1 and kflatc to $publish_dir"
fi

# Informational: whether the checked-in seed matches the current source. A
# stale seed is harmless until the tree needs something it cannot build.
if diff -q "$WORK/stage1.c" bootstrap/komp.c >/dev/null; then
    echo "OK: bootstrap/komp.c is current (matches a fresh self-build)"
    exit 0
fi

if [ "$REFRESH" -eq 0 ]; then
    echo "NOTE: bootstrap/komp.c is behind the current source."
    echo "      The fixpoint still holds, but consider refreshing the seed:"
    echo "        bootstrap/build.sh --refresh   # then commit"
    exit 0
fi

# stage1.c is the seed's output, and stage2 reproduced it byte for byte, so it
# is a fixpoint: installing it needs no further rebuild.
cp "$WORK/stage1.c" bootstrap/komp.c
echo "OK: refreshed bootstrap/komp.c from this run's verified stage1.c"
echo "    commit it on main by itself — see CONTRIBUTING.md."
