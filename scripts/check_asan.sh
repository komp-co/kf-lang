#!/usr/bin/env sh
# Sanitizer gate for memory correctness.
#
#   scripts/check_asan.sh <komp-binary> [WORKDIR]
#
# A leak changes nothing an assertion sees, and a use-after-free usually reads
# bytes still intact, so the corpus cannot gate this class.
#
# Each tests/asan/*.kf is a one-file project: built with komp, its emitted C
# recompiled under -fsanitize=address, run, and required to be clean.
#
# Directives, mirroring tests/cases:
#
#     //! asan: clean        must be sanitizer-clean (leaks included)
#     //! mode: unity        build via the legacy `komp <dir> <out.c>` form
#     //! broken: WHY        not clean yet; reported, not a failure
#
# A broken probe that comes back clean is a failure: the marker must go.
#
# CONSTRAINT: keep each probe as minimal as its repro, with no assertions.
# The sanitizer is the assertion and the exit code is not checked; an extra
# statement perturbs what gets dropped and can mask a leak. A probe whose
# figures differ from its issue's is wrong until proven otherwise.
set -eu

KOMP="${1:?usage: check_asan.sh <komp-binary> [workdir]}"
case "$KOMP" in /*) ;; *) KOMP="$(pwd)/$KOMP" ;; esac
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WORK="${2:-$(mktemp -d)}"
CC="${CC:-cc}"
ASANFLAGS="-O1 -g -fno-omit-frame-pointer -fsanitize=address -w"

fails=0
broken=0
clean=0

for case_file in "$ROOT"/tests/asan/*.kf; do
    name="$(basename "$case_file" .kf)"
    mode="separate"
    grep -q '^//! mode: unity' "$case_file" && mode="unity"
    why="$(sed -n 's|^//! broken: ||p' "$case_file" | head -1)"

    d="$WORK/asan-$name"
    rm -rf "$d"; mkdir -p "$d/src"
    printf '[project]\nname = "%s"\nkind = "bin"\n' "$name" > "$d/kf.toml"
    cp "$case_file" "$d/src/main.kf"

    note=""
    if [ "$mode" = unity ]; then
        # The unity output inlines the runtime, so it links with no natives —
        # but it does need kf_runtime.h beside it.
        if ! ( cd "$d" && "$KOMP" . "$d/unity.c" ) > "$d/build.log" 2>&1; then
            note="the build failed"
        else
            cp "$ROOT/compiler/target/kflat/kf_runtime.h" "$d/" 2>/dev/null || true
            ( cd "$d" && $CC $ASANFLAGS -I. -o "$d/probe" unity.c -lm ) > "$d/cc.log" 2>&1 \
                || note="the emitted C did not compile"
        fi
    else
        if ! ( cd "$d" && "$KOMP" build . ) > "$d/build.log" 2>&1; then
            note="the build failed"
        else
            ( cd "$d/target/kflat" && $CC $ASANFLAGS -I. -o "$d/probe" ./*.c \
                "$ROOT"/libs/core/native/core.c "$ROOT"/libs/core/native/core_hosted.c \
                "$ROOT"/libs/alloc/native/alloc.c "$ROOT"/libs/alloc/native/alloc_hosted.c \
                "$ROOT"/libs/std/native/*.c -lm ) > "$d/cc.log" 2>&1 \
                || note="the emitted C did not compile"
        fi
    fi

    if [ -n "$note" ]; then
        echo "  FAIL  $name: $note"
        tail -5 "$d/build.log" "$d/cc.log" 2>/dev/null | sed 's/^/        /'
        fails=$((fails+1))
        continue
    fi

    # CONSTRAINT: use_stacks=0. LeakSanitizer scans the stack conservatively,
    # so a stale pointer in a dead slot hides a leak. komp drops locals at
    # scope exit, so everything still allocated at exit is leaked.
    ASAN_OPTIONS=detect_leaks=1 LSAN_OPTIONS=use_stacks=0 "$d/probe" > "$d/run.log" 2>&1 || true
    if grep -q "ERROR: AddressSanitizer\|ERROR: LeakSanitizer" "$d/run.log"; then
        summary="$(grep -m1 "SUMMARY: AddressSanitizer" "$d/run.log" || echo 'sanitizer error')"
        if [ -n "$why" ]; then
            echo "  BROKEN  $name: $why"
            echo "          $summary"
            broken=$((broken+1))
        else
            echo "  FAIL  $name is not sanitizer-clean"
            echo "        $summary"
            sed -n '1,12p' "$d/run.log" | sed 's/^/        /'
            fails=$((fails+1))
        fi
    else
        if [ -n "$why" ]; then
            # The corpus rule: a broken marker that starts passing has to go.
            echo "  FAIL  $name is now CLEAN but still marked broken:"
            echo "        $why"
            echo "        Delete the \`//! broken:\` line and close the issue it names."
            fails=$((fails+1))
        else
            echo "  PASS  $name"
            clean=$((clean+1))
        fi
    fi
done

echo "  asan: $clean clean, $broken known-broken, $fails failed"
[ "$fails" -eq 0 ] || { echo "FAIL: sanitizer gate" >&2; exit 1; }
