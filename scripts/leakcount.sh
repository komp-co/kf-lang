#!/usr/bin/env sh
# Self-leak scorecard for the String de-magic work.
#
#   scripts/leakcount.sh [INPUT_PROJECT]      # default input: compiler/kf-core
#
# komp "leaks by design" (never frees short-lived allocations; everything stays
# reachable, so LSan reports nothing). The metric that matters is allocation
# VOLUME that is never freed. Allocation counts are DETERMINISTIC — the same
# compiler on the same input allocates identically every run — so NET is a
# clean integer and a String delta shows up as an exact decrease.
#
# We build kflatc once with a --wrap allocation counter linked in, and have
# komp run it on INPUT_PROJECT. NET = allocations - frees = objects still
# live at exit. Prints:  <INPUT>  ALLOC=.. FREE=.. NET=..
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"; cd "$ROOT"
INPUT="${1:-compiler/kf-core}"
WORK="$(mktemp -d)"; trap 'rm -rf "$WORK"' EXIT

# 1. The seed, and the komp it builds; komp drives the compile.
. "$ROOT/bootstrap/seed.sh"
seed_build "$WORK/s0" > /dev/null
"$WORK/s0/komp0" "$ROOT/compiler/komp" "$WORK/komp.c" > /dev/null 2>&1
cc -O2 -o "$WORK/komp" "$WORK/komp.c" 2>/dev/null

# 2. The compiler proper is kflatc, the process komp runs beside it, so that
#    is the binary built with the allocation counter wrapped in.
"$WORK/s0/komp0" "$ROOT/compiler/kflatc" "$WORK/kflatc.c" > /dev/null 2>&1
cc -O2 -o "$WORK/kflatc" "$WORK/kflatc.c" scripts/malloccount.c \
    -Wl,--wrap=malloc,--wrap=free,--wrap=realloc,--wrap=calloc 2>/dev/null

# 3. A unity build of INPUT; kflatc prints its counts at exit.
STATS="$("$WORK/komp" "$ROOT/$INPUT" "$WORK/out.c" 2>&1 >/dev/null \
    | grep -E '^ALLOC=' | tail -1)"
printf '%-20s %s\n' "$INPUT" "$STATS"
