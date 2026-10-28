#!/usr/bin/env bash
# Evaluate the optimizer on a real SMT machine.
#
#   bench/run_bench.sh [program.c] [runs]
#   HT_FLAGS="--cores 2" bench/run_bench.sh tests/mixed.c 7
#
# Variants (all built from the same source):
#   baseline   original program, OS scheduler, no pinning
#   naive      HT-Compass, creation-order pairing
#   optimized  HT-Compass, minimum-contention pairing
#   worst      HT-Compass, maximum-contention pairing (upper bound)
# Runs are interleaved round-robin to reduce drift; the median wall time is shown.
set -eu
cd "$(dirname "$0")/.."
SRC=${1:-tests/mixed.c}
RUNS=${2:-5}
EXTRA=${HT_FLAGS:-}
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

make -s
echo "Host: $(nproc) logical CPUs"
if command -v lscpu >/dev/null 2>&1; then
    lscpu | grep -E 'Model name|Thread\(s\) per core|Core\(s\) per socket|L1d|L2' || true
    tpc=$(lscpu | awk -F: '/Thread\(s\) per core/ {gsub(/ /,"",$2); print $2}')
    if [ "${tpc:-1}" -lt 2 ]; then
        echo "WARNING: this host has no SMT - pairing results are NOT meaningful here."
    fi
fi
echo "Program: $SRC   runs: $RUNS   extra flags: ${EXTRA:-none}"
echo

gcc -O2 -pthread -o "$TMP/baseline" "$SRC" -lm
for s in naive optimized worst; do
    # shellcheck disable=SC2086
    ./htcompiler "$SRC" -o "$TMP/$s.c" --json "$TMP/$s.json" --strategy $s --quiet $EXTRA
    gcc -O2 -pthread -o "$TMP/$s" "$TMP/$s.c" -lm
done
echo "Predicted contention (from the compiler's model):"
for s in naive optimized worst; do
    printf '  %-10s %s\n' "$s" "$(grep -o '"predicted_total_contention": [0-9.]*' "$TMP/$s.json" | head -1 | awk '{print $2}')"
done
echo

for v in baseline naive optimized worst; do : > "$TMP/$v.times"; done
for r in $(seq "$RUNS"); do
    for v in baseline naive optimized worst; do
        s=$(date +%s.%N); "$TMP/$v" >/dev/null 2>&1; e=$(date +%s.%N)
        awk -v s="$s" -v e="$e" 'BEGIN{printf "%.4f\n", e-s}' >> "$TMP/$v.times"
    done
done

median() { sort -n | awk '{a[NR]=$1} END{ if (NR%2) print a[(NR+1)/2]; else print (a[NR/2]+a[NR/2+1])/2 }'; }
base=$(median < "$TMP/baseline.times")
printf '%-10s %12s %10s\n' variant "median [s]" "vs baseline"
for v in baseline naive optimized worst; do
    m=$(median < "$TMP/$v.times")
    printf '%-10s %12.3f %9.2fx\n' "$v" "$m" "$(awk -v b="$base" -v m="$m" 'BEGIN{print b/m}')"
done
