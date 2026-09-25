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
# We build the FULL komp compiler once (with a --wrap allocation counter linked
# in) and run it on INPUT_PROJECT. NET = allocations - frees = objects still
# live at exit. Prints:  <INPUT>  ALLOC=.. FREE=.. NET=..
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"; cd "$ROOT"
INPUT="${1:-compiler/kf-core}"
WORK="$(mktemp -d)"; trap 'rm -rf "$WORK"' EXIT

# 1. Fresh seed from the checked-in C (never trust a stale binary).
cc -O2 -o "$WORK/k0" bootstrap/komp.c 2>/dev/null

# 2. Seed compiles the FULL compiler to C.
"$WORK/k0" "$ROOT/compiler/komp" "$WORK/stage.c" >/dev/null 2>&1

# 3. Compile the full komp with the allocation counter wrapped in.
cc -O2 -o "$WORK/komp_count" "$WORK/stage.c" scripts/malloccount.c \
    -Wl,--wrap=malloc,--wrap=free,--wrap=realloc,--wrap=calloc 2>/dev/null

# 4. Run the full komp on INPUT; the counter prints NET at exit.
STATS="$("$WORK/komp_count" "$ROOT/$INPUT" "$WORK/out.c" 2>&1 >/dev/null \
    | grep -E '^ALLOC=' | tail -1)"
printf '%-20s %s\n' "$INPUT" "$STATS"
