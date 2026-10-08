#!/usr/bin/env sh
# Bootstrap kflatc from the released seed and verify the self-hosting fixpoint.
# Needs a C compiler, and the network once to fetch the seed.
#
#   bootstrap/build.sh                   # build + fixpoint check
#   bootstrap/build.sh --seed-out DIR    # ... and write this tree's seed to DIR
#   KFLAT_SEED=seed.tar.gz bootstrap/build.sh   # start from a local seed
#   CC=clang bootstrap/build.sh          # pick a C compiler (default: cc)
#   CFLAGS=-O0 bootstrap/build.sh        # cheaper cc, a far slower kflatc
#
# The seed is a released kflatc as C, and komp0 the komp that drives every
# build, both pinned by bootstrap/stage0.toml; bootstrap/seed.sh fetches them.
# komp0 only drives: the C is emitted by the kflatc it runs, named by KFLATC.
#
# Chain:
#   1. cc the seed                          -> kflatc0, beside komp0
#   2. kflatc0 compiler/kflatc -> kflatc1.c ; cc -> kflatc1
#   3. kflatc1 compiler/kflatc -> kflatc2.c
#   4. assert kflatc1.c == kflatc2.c (the fixpoint)
#
# kflatc1.c is the seed's output, so a tree that changes what the compiler
# emits for itself cannot match it. Then kflatc2 is built from kflatc2.c,
# builds the tree once more, and the fixpoint is kflatc2.c == kflatc3.c: two
# compilers from the same source agree.
#
# `--seed-out` writes the release archives packed from the kflatc the fixpoint
# proved: kflat-seed-<version>.tar.gz, and kflat-<version>.tar.gz, which
# installs it as a toolchain. A release publishes both. The install archive is
# checked on every run, by installing it and building a program with it.
#
# Each step fails for its own reason, named with its remedy.
#
# Exit non-zero if any stage or the fixpoint fails. Run this in CI.
set -eu

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
CC="${CC:-cc}"
CFLAGS="${CFLAGS:--O2}"

SEED_OUT=""
while [ $# -gt 0 ]; do
    case "$1" in
        --seed-out) [ $# -ge 2 ] || { echo "usage: $0 [--seed-out DIR]" >&2; exit 2; }; SEED_OUT="$2"; shift ;;
        *) echo "usage: $0 [--seed-out DIR]" >&2; exit 2 ;;
    esac
    shift
done
. "$ROOT/bootstrap/seed.sh"

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
# build machine almost always the OOM killer: this script's peak is one
# self-compile of kflatc.
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
        echo "  your source. bootstrap/build.sh peaks at one whole self-compile;"
        echo "  check free memory, and whether another build or CI job was"
        echo "  running at the same time."
    fi
    exit 1
}

# One TIME: line per step, which scripts/check.sh reports.
step_t0="$(date +%s)"
step_time() {
    step_now="$(date +%s)"
    echo "TIME: $(( step_now - step_t0 ))s $1"
    step_t0="$step_now"
}

echo "[1/4] $CC the seed, kflat $(seed_field version) -> kflatc0, beside the driver komp0"
if ! seed_build "$WORK/s0"; then
    fail "the seed did not build." \
         "Either it could not be fetched or verified (the lines above say which)," \
         "or it does not compile with $CC: it is generated C that a working komp" \
         "emitted, so a failure there is about the C toolchain, not your source."
fi

# The positional build below does not fetch: `metadata` fetches what
# compiler/kf.lock pins into the cache first.
"$WORK/s0/komp0" metadata "$ROOT/compiler" > /dev/null
step_time "the seed"

# kflatc_c NAME KFLATC OUT — the driver compiles compiler/kflatc into the C
# file OUT, running the kflatc KFLATC; NAME says which compiler that is, for
# the failure.
kflatc_c() {
    kflatc_c_status=0
    KFLATC="$2" "$WORK/s0/komp0" "$ROOT/compiler/kflatc" "$3" || kflatc_c_status=$?
    if [ "$kflatc_c_status" -ne 0 ]; then
        report_if_killed "$kflatc_c_status" "$1 reading compiler/kflatc" || true
        fail "$1 rejected compiler/kflatc." \
             "The errors above come from $1 reading this tree. When it is the" \
             "seed, the source may use a construct the pinned seed predates:" \
             "see \"The bootstrap seed\" in CONTRIBUTING.md."
    fi
}

# cc_kflatc C OUT — compiles one kflatc from its C.
cc_kflatc() {
    cc_dir="$(dirname "$2")"
    mkdir -p "$cc_dir"
    # From inside `$WORK`, so its random name stays out of the hashed command line.
    if ! (cd "$WORK" && "$CC" $CFLAGS -c -o "$2.o" "$1") || ! "$CC" $CFLAGS -o "$2" "$2.o"; then
        fail "$(basename "$1") does not compile." \
             "The errors above are in generated C, not your source: the compiler" \
             "that emitted it miscompiles something this tree now depends on."
    fi
}

echo "[2/4] the seed's kflatc compiler/kflatc -> kflatc1.c ; $CC -> kflatc1"
kflatc_c "the seed" "$WORK/s0/kflatc" "$WORK/kflatc1.c"
cc_kflatc "$WORK/kflatc1.c" "$WORK/s1/kflatc"
step_time "stage 1, built by the seed"

echo "[3/4] kflatc1 compiler/kflatc -> kflatc2.c"
kflatc_c "the compiler this tree builds" "$WORK/s1/kflatc" "$WORK/kflatc2.c"
step_time "stage 2, emitted by stage 1"

echo "[4/4] fixpoint check"
# The compiler that proved the fixpoint and the C it reproduced.
proven_kflatc="$WORK/s1/kflatc"
proven_kflatc_c="$WORK/kflatc1.c"
if ! diff -q "$WORK/kflatc1.c" "$WORK/kflatc2.c" >/dev/null; then
    echo "NOTE: this tree changes what the compiler emits for itself; checking one stage later"
    cc_kflatc "$WORK/kflatc2.c" "$WORK/s2/kflatc"
    kflatc_c "the compiler built by the current compiler" "$WORK/s2/kflatc" "$WORK/kflatc3.c"
    step_time "stage 3, built and emitted by stage 2"
    if ! diff -q "$WORK/kflatc2.c" "$WORK/kflatc3.c" >/dev/null; then
        echo "FAIL: fixpoint broken (kflatc2.c != kflatc3.c)"
        echo "  the compiler does not reproduce itself; see diff:"
        diff "$WORK/kflatc2.c" "$WORK/kflatc3.c" | head -40
        exit 1
    fi
    proven_kflatc="$WORK/s2/kflatc"
    proven_kflatc_c="$WORK/kflatc2.c"
    echo "OK: kflatc2.c == kflatc3.c (fixpoint holds)"
else
    echo "OK: kflatc1.c == kflatc2.c (fixpoint holds)"
fi

# KOMP_PUBLISH names a path to copy the driver komp to, with the verified
# kflatc beside it, so other jobs can reuse both instead of rebuilding them
# from the seed. Each is written beside its target and renamed, so a reader
# sees the whole binary or none.
if [ -n "${KOMP_PUBLISH:-}" ]; then
    publish_dir="$(dirname "$KOMP_PUBLISH")"
    mkdir -p "$publish_dir"
    cp "$proven_kflatc" "$publish_dir/kflatc.tmp.$$" && mv -f "$publish_dir/kflatc.tmp.$$" "$publish_dir/kflatc"
    cp "$WORK/s0/komp0" "$KOMP_PUBLISH.tmp.$$" && mv -f "$KOMP_PUBLISH.tmp.$$" "$KOMP_PUBLISH"
    echo "OK: published the driver komp and the verified kflatc to $publish_dir"
fi

# Informational: whether the pinned seed matches the current source. A stale
# seed is harmless until the tree needs something it cannot build.
if diff -q "$proven_kflatc_c" "$WORK/s0/seed/kflatc.c" > /dev/null; then
    echo "OK: the seed is current (it matches a fresh self-build)"
else
    echo "NOTE: the seed is behind the current source; a release would catch it up."
fi

# The release archives, packed from what the fixpoint proved: the seed, which
# bootstraps the next tree, and the install archive, the seed with the
# libraries and bootstrap/install.sh beside it. Byte-identical from identical
# input: sorted, a fixed date, no owner, gzip -n. A release is named by the
# compiler's version; komp has its own.
version="$("$proven_kflatc" version | sed -n 's/^kflatc //p' | head -1)"
pack() {
    (cd "$WORK/out" && tar --sort=name --mtime='2000-01-01 00:00Z' --owner=0 --group=0 --numeric-owner -cf - "$1" \
        | gzip -n -9 > "$1.tar.gz")
}
seed_name="kflat-seed-$version"
install_name="kflat-$version"
rm -rf "$WORK/out"
mkdir -p "$WORK/out/$seed_name" "$WORK/out/$install_name"
cp "$proven_kflatc_c" "$WORK/out/$seed_name/kflatc.c"
pack "$seed_name"
cp "$proven_kflatc_c" "$WORK/out/$install_name/kflatc.c"
cp "$ROOT/bootstrap/install.sh" "$WORK/out/$install_name/install.sh"
echo "$version" > "$WORK/out/$install_name/VERSION"
# The libraries as tracked, so no build output rides along.
if git -C "$ROOT" rev-parse --git-dir > /dev/null 2>&1; then
    (cd "$ROOT" && git ls-files libs) > "$WORK/out/libs.list"
else
    (cd "$ROOT" && find libs -type f ! -path '*/target/*') > "$WORK/out/libs.list"
fi
(cd "$ROOT" && tar -cf - -T "$WORK/out/libs.list") | tar -xf - -C "$WORK/out/$install_name"
pack "$install_name"

# The install archive installs, and what it installs builds a program that
# uses std. At -O0, since this proves the archive, not the optimizer, and the
# fixpoint compiled the same C already.
echo "      checking that $install_name.tar.gz installs"
mkdir -p "$WORK/install-check/unpacked" "$WORK/install-check/app/src"
tar -xzf "$WORK/out/$install_name.tar.gz" -C "$WORK/install-check/unpacked"
if ! KFLAT_HOME="$WORK/install-check/home" CFLAGS=-O0 sh "$WORK/install-check/unpacked/$install_name/install.sh" \
        > "$WORK/install-check/install.log" 2>&1; then
    cat "$WORK/install-check/install.log"
    fail "the install archive does not install; bootstrap/install.sh failed as above."
fi
printf '[project]\nname = "installed"\nversion = "0.1.0"\nkind = "bin"\n' > "$WORK/install-check/app/kf.toml"
printf 'import std.io.eprintln\n\nfun main(): int32 {\n    eprintln(&"installed")\n    return 0\n}\n' \
    > "$WORK/install-check/app/src/main.kf"
# The driver komp builds with the installed kflatc and the libraries beside
# it in its toolchain.
installed_kflatc="$WORK/install-check/home/toolchains/$version/bin/kflatc"
[ -x "$installed_kflatc" ] && [ -L "$WORK/install-check/home/bin/kflatc" ] ||
    fail "install.sh did not install kflatc into toolchains/$version and link it into bin."
for tool in kf-lint kf-editor; do
    "$WORK/install-check/home/bin/$tool" version | grep -qx "$tool $version" ||
        fail "the installed $tool does not answer as $tool $version."
done
if ! (cd "$WORK/install-check" && KFLATC="$installed_kflatc" "$WORK/s0/komp0" run app > run.log 2>&1) ||
        ! grep -qx installed "$WORK/install-check/run.log"; then
    cat "$WORK/install-check/run.log"
    fail "komp could not build and run a program that uses std with the installed kflatc."
fi
echo "OK: $install_name.tar.gz installs, and komp builds a program with what it installed"
step_time "the install check"

if [ -n "$SEED_OUT" ]; then
    mkdir -p "$SEED_OUT"
    for archive in "$seed_name" "$install_name"; do
        cp "$WORK/out/$archive.tar.gz" "$SEED_OUT/"
        (cd "$SEED_OUT" && seed_sha256 "$archive.tar.gz" | sed "s/\$/  $archive.tar.gz/" > "$archive.tar.gz.sha256")
        echo "OK: wrote $SEED_OUT/$archive.tar.gz"
    done
fi
