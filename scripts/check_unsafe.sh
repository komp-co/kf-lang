#!/usr/bin/env sh
# A ratchet on UNSAFE BLOCKS in the compiler's source, the twin of
# check_line_lengths.sh: a file may not gain an `unsafe {`, and a file with
# none may not grow one. Shrinking is always allowed.
#
# Tests are left out: they reach for `unsafe { system(...) }` to lay out
# scratch projects. The runtime under libs/ is where unsafe belongs.
set -eu

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
BASELINE="scripts/unsafe_baseline.txt"

counts() {
    find compiler -name target -prune -o -name '*.kf' ! -name '*_test.kf' -type f -print | sort |
    while read -r f; do
        n=$(grep -o 'unsafe *{' "$f" | wc -l | tr -d ' ')
        if [ "$n" -gt 0 ]; then printf '%s %s\n' "$n" "$f"; fi
    done
}

if [ "${1:-}" = "--update" ]; then
    {
        echo "# \`unsafe {\` blocks in the compiler's source, and the count each file is"
        echo "# frozen at. Regenerate with scripts/check_unsafe.sh --update."
        echo "#"
        echo "# A count here is not permission to write another block; it records what"
        echo "# was already there. Shrinking is always allowed; growing needs this file"
        echo "# updated in the same commit, which is the point at which someone has to"
        echo "# look."
        counts
    } > "$BASELINE"
    total=$(grep -v '^#' "$BASELINE" | awk '{ s += $1 } END { print s + 0 }')
    echo "wrote $BASELINE ($total unsafe blocks in $(grep -cv '^#' "$BASELINE") files)"
    exit 0
fi

if [ ! -f "$BASELINE" ]; then
    echo "FAIL: $BASELINE is missing; run scripts/check_unsafe.sh --update" >&2
    exit 1
fi

# The counter crosses the loop's subshell through a file, so clear any left by
# an interrupted run.
rm -f "$BASELINE.fail"

counts | while read -r n f; do
    was=$(awk -v f="$f" '$2 == f { print $1 }' "$BASELINE")
    if [ -z "$was" ]; then was=0; fi
    if [ "$n" -gt "$was" ]; then
        echo "  MORE  $f: $was -> $n unsafe blocks" >&2
        echo x >> "$BASELINE.fail"
    fi
done

if [ -f "$BASELINE.fail" ]; then
    fails=$(wc -l < "$BASELINE.fail" | tr -d ' ')
    rm -f "$BASELINE.fail"
    echo "FAIL: $fails file(s) gained an unsafe block." >&2
    echo "      Own the value (Box<T>), borrow it (&T, &var T), or call the library" >&2
    echo "      instead of an extern; if the block is genuinely needed, run" >&2
    echo "      scripts/check_unsafe.sh --update and say why in the commit." >&2
    exit 1
fi

echo "OK: no compiler file gained an unsafe block"
