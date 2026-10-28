#!/usr/bin/env bash
# Regression tests for HT-Compass.
#   1. every tests/*.c is compiled by htcompiler, the generated C is built with
#      gcc -Wall -Werror -pthread, and original vs generated output is compared
#   2. option matrix (strategy x policy) on tests/mixed.c
#   3. model sanity checks (pairing quality, no-overlap => zero contention)
#   4. every tests/errors/*.c must be rejected with the expected diagnostic
set -u
cd "$(dirname "$0")/.."
HTC=./htcompiler
TOPO="0,4;1,5;2,6;3,7"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
pass=0; fail=0
ok()  { echo "  PASS  $1"; pass=$((pass+1)); }
bad() { echo "  FAIL  $1"; fail=$((fail+1)); }

[ -x $HTC ] || { echo "build first: make"; exit 1; }

echo "== 1. positive programs =="
for src in tests/*.c; do
    n=$(basename "$src" .c)
    if ! $HTC "$src" -o "$TMP/$n.gen.c" --json "$TMP/$n.json" --topology "$TOPO" --quiet 2>"$TMP/$n.err"; then
        bad "$n: htcompiler rejected a valid program"; cat "$TMP/$n.err"; continue
    fi
    if ! gcc -O1 -pthread -Wall -o "$TMP/$n.orig" "$src" -lm 2>"$TMP/$n.cc1"; then
        bad "$n: original does not build"; cat "$TMP/$n.cc1"; continue
    fi
    if ! gcc -O1 -pthread -Wall -Werror -o "$TMP/$n.gen" "$TMP/$n.gen.c" -lm 2>"$TMP/$n.cc2"; then
        bad "$n: generated code does not build"; cat "$TMP/$n.cc2"; continue
    fi
    "$TMP/$n.orig" 2>&1 | sort > "$TMP/$n.o1"
    "$TMP/$n.gen"  2>&1 | sort > "$TMP/$n.o2"
    if cmp -s "$TMP/$n.o1" "$TMP/$n.o2"; then ok "$n: builds, same output as original"
    else bad "$n: output differs"; diff "$TMP/$n.o1" "$TMP/$n.o2" | head; fi
    if command -v python3 >/dev/null 2>&1; then
        if python3 -c "import json,sys; json.load(open('$TMP/$n.json'))" 2>/dev/null; then ok "$n: JSON report is valid"
        else bad "$n: JSON report is invalid"; fi
    fi
done

echo "== 2. option matrix on mixed.c =="
for strat in optimized naive worst; do
    for pol in pack spread; do
        tag="mixed[$strat/$pol]"
        if $HTC tests/mixed.c -o "$TMP/m.c" --json "$TMP/m.json" --topology "$TOPO" \
              --strategy $strat --policy $pol --quiet 2>/dev/null &&
           gcc -O1 -pthread -Wall -Werror -o "$TMP/m" "$TMP/m.c" 2>/dev/null &&
           "$TMP/m" >/dev/null 2>&1; then ok "$tag"; else bad "$tag"; fi
    done
done
$HTC tests/mixed.c -o "$TMP/m.c" --json "$TMP/m.json" --topology "$TOPO" --cores 2 --policy spread --quiet 2>/dev/null \
    && ok "mixed[--cores 2 --policy spread]" || bad "mixed[--cores 2 --policy spread]"

echo "== 3. model sanity =="
out=$($HTC tests/mixed.c -o "$TMP/m.c" --json "$TMP/m.json" --topology "$TOPO" 2>/dev/null)
n=$(echo "$out" | grep -cE 'core [0-9]+ \(CPU [0-9 ]+\): T[0-9] stream_. \+ T[0-9] fp_compute_.')
[ "$n" = 2 ] && ok "mixed: each streaming thread is paired with a compute thread" \
             || bad "mixed: expected stream+compute pairing, got $n such cores"
opt=$(grep -o '"optimized_total": [0-9.]*' "$TMP/m.json" | awk '{print $2}')
nai=$(grep -o '"naive_total": [0-9.]*' "$TMP/m.json" | awk '{print $2}')
awk -v o="$opt" -v n="$nai" 'BEGIN{exit !(o < n)}' && ok "mixed: optimized ($opt) < naive ($nai)" \
                                                    || bad "mixed: optimized ($opt) not below naive ($nai)"
$HTC tests/sequential.c -o "$TMP/s.c" --json "$TMP/s.json" --topology "$TOPO" --quiet 2>/dev/null
tot=$(grep -o '"optimized_total": [0-9.]*' "$TMP/s.json" | awk '{print $2}')
awk -v t="$tot" 'BEGIN{exit !(t == 0)}' && ok "sequential: non-overlapping threads => zero contention" \
                                         || bad "sequential: expected 0, got $tot"
out=$($HTC tests/patterns.c -o "$TMP/p.c" --json "$TMP/p.json" --topology "$TOPO" 2>/dev/null)
echo "$out" | grep -q "chase .*latency-bound" && ok "patterns: pointer chasing classified latency-bound" \
                                              || bad "patterns: chase not latency-bound"
echo "$out" | grep -q "col_walk .*memory-bound" && ok "patterns: column walk classified memory-bound" \
                                              || bad "patterns: col_walk not memory-bound"
out=$($HTC tests/loop_threads.c -o "$TMP/l.c" --json "$TMP/l.json" --topology "$TOPO" 2>/dev/null)
echo "$out" | grep -q "Threads  : 6 (from 2 creation site" && ok "loop_threads: 4+2 loop instances expanded to 6 threads" \
                                                          || bad "loop_threads: wrong thread count"
grep -q '__ht_create(&streamers\[i\], streamer, NULL, i)' "$TMP/l.gen.c" 2>/dev/null || \
grep -q '__ht_create(&streamers\[i\], streamer, NULL, i)' "$TMP/l.c" && ok "loop_threads: loop var used as thread index" \
                                                                      || bad "loop_threads: index expression wrong"

echo "== 4. rejected programs =="
for f in tests/errors/*.c; do
    n=$(basename "$f" .c)
    exp=$(head -1 "$f" | sed 's#^// EXPECT: ##')
    if $HTC "$f" -o "$TMP/e.c" --json "$TMP/e.json" --topology "$TOPO" --quiet 2>"$TMP/e.err"; then
        bad "$n: should have been rejected"
    elif grep -qF -- "$exp" "$TMP/e.err"; then ok "$n: rejected ($exp)"
    else bad "$n: wrong diagnostic"; echo "      expected: $exp"; head -2 "$TMP/e.err" | sed 's/^/      got:      /'; fi
done

echo "== 5. optimization pipeline: legality verdicts (--opt-plan builds and runs nothing) =="
plan_has() {   # name file regex
    ./htcompiler "$2" --opt-plan -o "$TMP/p.c" --json "$TMP/p.json" --topology "$TOPO" > "$TMP/plan.txt" 2>/dev/null
    if grep -Eq -- "$3" "$TMP/plan.txt"; then ok "$1"; else bad "$1"; sed -n '/^Optimization/,/^$/p' "$TMP/plan.txt" | head -12; fi
}
plan_has "transpose: column nest is a legal interchange"        tests/opt/transpose.c            "found +interchange +swap the loops over 'j' and 'i'"
plan_has "illegal_interchange: reversed dependence is refused"  tests/opt/illegal_interchange.c  "would be reversed"
plan_has "fusion: legal pair found"                             tests/opt/fusion.c               "found +fusion"
plan_has "fusion: pair that reads x[i+1] is refused"            tests/opt/fusion.c               "before loop 1 has produced"
plan_has "promotion: invariant c[i][j] found"                   tests/opt/promotion.c            "found +promotion +keep c\\[i\\]\\[j\\]"
plan_has "promotion: possible alias d[i][j+1] blocks interchange" tests/opt/promotion.c          "d\\[i\\]\\[\\(j\\+1\\)\\] and d\\[i\\]\\[j\\] would be reversed"
plan_has "fp_sum: reordering a float sum needs --fp-reassoc"    tests/opt/fp_sum.c               "allow it with --fp-reassoc"
plan_has "threads_shared: promotion on a shared global refused" tests/opt/threads_shared.c       "share global array 'shared'"
n=$(./htcompiler tests/opt/nothing.c --opt-plan -o "$TMP/p.c" --json "$TMP/p.json" --topology "$TOPO" 2>/dev/null | grep -cE '^  (found|rejected|skipped|applied)')
[ "$n" = 0 ] && ok "nothing: no opportunities reported" || bad "nothing: reported $n"

echo "== 6. optimization pipeline: apply, verify, build (needs gcc) =="
if command -v gcc >/dev/null 2>&1; then
    # transpose: the column nest is interchanged, the optimized program prints the same result
    ./htcompiler tests/opt/transpose.c --offline-optimize --no-bench -o "$TMP/t_gen.c" --json "$TMP/t.json" \
        --topology "$TOPO" --emit-opt "$TMP/t_opt.c" > "$TMP/t.txt" 2>/dev/null
    grep -Eq "applied +interchange" "$TMP/t.txt" && ok "transpose: interchange applied" || { bad "transpose: nothing applied"; sed -n '/^Optimization/,/^$/p' "$TMP/t.txt" | head; }
    grep -Eq "run\(s\) agreed" "$TMP/t.txt" && ok "transpose: differential test ran and agreed" || bad "transpose: no differential test"
    if gcc -O2 -w -o "$TMP/t0" tests/opt/transpose.c && gcc -O2 -w -o "$TMP/t1" "$TMP/t_opt.c"; then
        [ "$("$TMP/t0")" = "$("$TMP/t1")" ] && ok "transpose: optimized program prints the same output" || bad "transpose: output differs"
    else bad "transpose: build failed"; fi

    # an illegal change forced past the static proof must be caught by the differential test
    HT_TEST_FORCE_UNSAFE=1 ./htcompiler tests/opt/illegal_interchange.c --offline-optimize --no-bench -o "$TMP/f.c" \
        --json "$TMP/f.json" --topology "$TOPO" > "$TMP/f.txt" 2>/dev/null
    grep -q "DIFFERENTIAL TEST FAILED" "$TMP/f.txt" && ok "forced-illegal interchange is caught by the differential test" \
        || { bad "forced-illegal interchange was NOT caught"; sed -n '/^Optimization/,/^$/p' "$TMP/f.txt" | head; }
    ! grep -Eq "^  applied +interchange" "$TMP/f.txt" && ok "forced-illegal interchange was not applied" || bad "forced-illegal interchange was applied"

    # floating-point reordering: refused by default, accepted only with --fp-reassoc
    ./htcompiler tests/opt/fp_sum.c --offline-optimize --no-bench -o "$TMP/s.c" --json "$TMP/s.json" --topology "$TOPO" > "$TMP/s.txt" 2>/dev/null
    ! grep -Eq "^  applied +interchange" "$TMP/s.txt" && ok "fp_sum: interchange refused without --fp-reassoc" || bad "fp_sum: interchange applied without permission"
    ./htcompiler tests/opt/fp_sum.c --offline-optimize --no-bench --fp-reassoc -o "$TMP/s2.c" --json "$TMP/s2.json" --topology "$TOPO" > "$TMP/s2.txt" 2>/dev/null
    grep -Eq "applied +interchange" "$TMP/s2.txt" && ok "fp_sum: applied with --fp-reassoc (tolerance test)" || { bad "fp_sum: not applied with --fp-reassoc"; sed -n '/^Optimization/,/^$/p' "$TMP/s2.txt" | head; }

    # threads: workers are optimized, then mapped and pinned; the result builds warning-free
    ./htcompiler tests/opt/threads_walk.c --offline-optimize --no-bench -o "$TMP/w_gen.c" --json "$TMP/w.json" --topology "$TOPO" > "$TMP/w.txt" 2>/dev/null
    [ "$(grep -cE 'applied +interchange' "$TMP/w.txt")" -ge 1 ] && ok "threads_walk: interchange applied inside a thread worker" || bad "threads_walk: nothing applied"
    grep -q "__ht_create" "$TMP/w_gen.c" && ok "threads_walk: optimized program is also pinned" || bad "threads_walk: no pinning code"
    gcc -O1 -pthread -Wall -Werror -o "$TMP/w_bin" "$TMP/w_gen.c" 2>"$TMP/w.err" && [ "$("$TMP/w_bin")" = "1.000000 3.000000" ] \
        && ok "threads_walk: optimized+pinned program builds and prints the right result" || { bad "threads_walk: build or output wrong"; head -5 "$TMP/w.err"; }

    # shared global written by two instances: nothing may be promoted
    ./htcompiler tests/opt/threads_shared.c --offline-optimize --no-bench -o "$TMP/h.c" --json "$TMP/h.json" --topology "$TOPO" > "$TMP/h.txt" 2>/dev/null
    ! grep -Eq "^  applied +promotion" "$TMP/h.txt" && ok "threads_shared: the shared array was not promoted to a register" || bad "threads_shared: shared array promoted"
else
    echo "  (gcc not found: skipping sections 6)"
fi

echo "== 7. soundness fuzzing of the dependence analysis (needs python3) =="
if command -v python3 >/dev/null 2>&1; then
    if python3 tests/fuzz_opt.py 300 1 > "$TMP/fuzz.txt" 2>&1; then
        ok "fuzz: 300 random nests, no unsound interchange or fusion ($(grep -E 'judged legal' "$TMP/fuzz.txt" | awk '{s+=$5} END{print s}') legal verdicts checked)"
    else bad "fuzz: soundness violation"; head -30 "$TMP/fuzz.txt"; fi
else
    echo "  (python3 not found: skipping)"
fi

echo "== 8. unit tests: bounded JSON parser (model output is untrusted input) =="
if make -s unit > "$TMP/unit.txt" 2>&1; then
    n=$(grep -o "[0-9]* checks" "$TMP/unit.txt" | head -1)
    ok "json parser: $n, all passed"
else bad "json parser unit tests"; cat "$TMP/unit.txt" | tail -8; fi

echo "== 9. local-model integration with MOCKED replies (tests/run_llm_tests.sh) =="
if command -v python3 >/dev/null 2>&1; then
    ./tests/run_llm_tests.sh > "$TMP/llm.txt" 2>&1
    llm_rc=$?
    grep -E "FAIL" "$TMP/llm.txt" | head -10
    llm_line=$(grep -E "^mocked AI tests" "$TMP/llm.txt")
    p=$(echo "$llm_line" | sed -n 's/.*passed: \([0-9]*\).*/\1/p'); f=$(echo "$llm_line" | sed -n 's/.*failed: \([0-9]*\).*/\1/p')
    pass=$((pass + ${p:-0})); fail=$((fail + ${f:-0}))
    echo "  mocked AI tests: ${p:-0} passed, ${f:-0} failed   (replies from fixtures / tests/mock_ollama.py: NOT real inference)"
    [ "$llm_rc" = 0 ] || [ "${f:-1}" != 0 ] || bad "mocked AI test script exited with $llm_rc"
else
    echo "  (python3 not found: skipping)"
fi
echo "== 10. REAL local-model inference =="
echo "  not part of 'make test' (it needs Ollama and an installed model, and is slow). Run:  ./run.sh llm-test"
echo "  (it reports SKIPPED, never PASSED, when no model is available)"

echo
echo "passed: $pass   failed: $fail"
[ "$fail" = 0 ]
