#!/usr/bin/env sh
# The front-end fuzz gate: kflatc must end every run with diagnostics, however
# broken its input. A crash, panic or hang is a finding.
#
#   scripts/check_fuzz.sh <komp-binary> [WORKDIR]
#
# Builds tools/kf-fuzz with that komp and runs it with the kflatc beside it:
# fixtures from tests/cases mutated, then library interfaces mutated, over a
# fixed range of seeds. A finding tools/kf-fuzz/known.txt names is counted and
# passes; any other fails the gate, with the seed that reproduces it.
#
# FUZZ_FRESH=1 runs fresh seeds instead, for FUZZ_BUDGET seconds per mode
# (default 600): the nightly job. Its findings are saved under
# WORKDIR/fuzz-findings.
set -eu

KOMP="${1:?usage: check_fuzz.sh <komp-binary> [workdir]}"
case "$KOMP" in /*) ;; *) KOMP="$(pwd)/$KOMP" ;; esac
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WORK="${2:-$(mktemp -d)}"
mkdir -p "$WORK"
KFLATC="$(dirname "$KOMP")/kflatc"
JOBS="$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)"

if ! "$KOMP" build "$ROOT/tools/kf-fuzz" > "$WORK/kf-fuzz-build.log" 2>&1; then
    cat "$WORK/kf-fuzz-build.log" >&2
    echo "FAIL: tools/kf-fuzz does not build" >&2
    exit 1
fi
FUZZ="$ROOT/tools/kf-fuzz/target/kflat/kf_fuzz"

if [ "${FUZZ_FRESH:-0}" = "1" ]; then
    first="$(date +%s)000"
    seeds="$first..$((first + 999999))"
    extra="--budget ${FUZZ_BUDGET:-600}"
else
    seeds="1..1500"
    extra=""
fi

cd "$ROOT"
status=0
for mode in front kfi; do
    # shellcheck disable=SC2086
    "$FUZZ" "$mode" --seeds "$seeds" --jobs "$JOBS" --timeout 5 --kflatc "$KFLATC" \
        --save "$WORK/fuzz-findings" $extra || status=$?
done
if [ "$status" -ne 0 ]; then
    echo "FAIL: the fuzzer found something new. Rebuild a finding with" >&2
    echo "      kf_fuzz front --seeds N..N --emit DIR, shrink it with tools/kf-reduce, and" >&2
    echo "      fix it, or file it and add its signature to tools/kf-fuzz/known.txt." >&2
    exit 1
fi
