#!/usr/bin/env bash
# Tests of the local-model integration that do NOT need a model.
#
#   MOCKED  - the model's reply comes from a fixture file (--llm-reply-file) or from
#             tests/mock_ollama.py, a stand-in that replays canned text.  These tests check
#             how htcompiler VALIDATES, REJECTS and REPORTS what a model might say.  None of
#             them is real inference; the report says so ("reply-file ... not real inference").
#   REAL    - tests/llm_integration.sh, which talks to a genuinely installed model.
#
# Needs python3 (JSON assertions, the mock server).  Section B also needs gcc.
set -u
cd "$(dirname "$0")/.."
HTC=./htcompiler
TOPO="0,4;1,5"
FX=tests/llm
TMP=$(mktemp -d)
MOCK_PID=""
trap '[ -n "$MOCK_PID" ] && kill $MOCK_PID 2>/dev/null; rm -rf "$TMP"; rm -f /tmp/HT_COMPASS_PWNED' EXIT
pass=0; fail=0
ok()  { echo "  PASS  $1"; pass=$((pass+1)); }
bad() { echo "  FAIL  $1"; fail=$((fail+1)); [ -n "${2:-}" ] && echo "        $2"; }

command -v python3 >/dev/null 2>&1 || { echo "  (python3 not found: skipping the mocked AI tests)"; exit 0; }
[ -x $HTC ] || { echo "build first: make"; exit 1; }

# q FILE "python expression over d"
q() { python3 - "$1" "$2" <<'EOF'
import json, sys
d = json.load(open(sys.argv[1]))
print(eval(sys.argv[2]))
EOF
}
# plan_ai NAME PROGRAM REPLY [extra args...]  -> $TMP/r.json, $TMP/r.txt, $RC
plan_ai() {
    local prog=$1 reply=$2; shift 2
    rm -f "$TMP/r.json" "$TMP/r.txt"
    $HTC "$prog" --opt-plan --ai --llm-reply-file "$reply" -o "$TMP/r.c" --json "$TMP/r.json" --topology "$TOPO" "$@" > "$TMP/r.txt" 2> "$TMP/r.err"
    RC=$?
}
has() { grep -q -- "$1" "$2"; }

echo "== A. model replies from fixture files (MOCKED; validation and reporting) =="

plan_ai tests/opt/transpose.c $FX/valid_transpose.json
[ "$RC" = 0 ] && ok "valid reply: accepted" || bad "valid reply: exit $RC" "$(head -3 $TMP/r.err)"
[ "$(q $TMP/r.json "d['optimization']['ai']['mode']")" = "reply-file (test hook, not real inference)" ] && ok "valid reply: report says it is a test reply, not real inference" || bad "valid reply: mode label"
[ "$(q $TMP/r.json "d['optimization']['ai']['proposals']['total']")" = 2 ] && ok "valid reply: 2 proposals" || bad "valid reply: proposal count"
[ "$(q $TMP/r.json "[c['status'] for c in d['optimization']['candidates'] if c['source']=='model' and c['transformation']=='interchange'][0]")" = found ] && ok "valid reply: interchange attached to the compiler's own candidate" || bad "valid reply: not attached"
[ "$(q $TMP/r.json "[c['status'] for c in d['optimization']['candidates'] if c['transformation']=='tiling'][0]")" = unsupported ] && ok "unsupported proposal (tiling) reported as proposed but unsupported" || bad "tiling not marked unsupported"
[ "$(q $TMP/r.json "[c['model_proposal']['rank'] for c in d['optimization']['candidates'] if c['source']=='model' and c['transformation']=='interchange'][0]")" = 1 ] && ok "the model's explanation and rank are carried into the report" || bad "model_proposal missing"

plan_ai tests/opt/transpose.c $FX/malformed_prose.txt
[ "$RC" = 0 ] && ok "malformed reply (prose, no JSON): not fatal" || bad "malformed prose: exit $RC"
[ "$(q $TMP/r.json "d['optimization']['ai']['proposals']['total']")" = 0 ] && ok "malformed reply: zero proposals recorded" || bad "malformed: proposals"
q $TMP/r.json "d['optimization']['ai']['validation_notes']" | grep -q "no JSON" && ok "malformed reply: the problem is reported" || bad "malformed: no note"
[ "$(q $TMP/r.json "sum(1 for c in d['optimization']['candidates'] if c['source']=='compiler')")" -ge 1 ] && ok "malformed reply: the compiler's own findings still reported (not attributed to the model)" || bad "malformed: no compiler candidates"

plan_ai tests/opt/transpose.c $FX/truncated.json
q $TMP/r.json "d['optimization']['ai']['validation_notes']" | grep -q "incomplete" && ok "cut-off JSON detected" || bad "truncated not detected"

plan_ai tests/opt/transpose.c $FX/wrong_shape.json
q $TMP/r.json "d['optimization']['ai']['validation_notes']" | grep -q "no .proposals. list" && ok "JSON of the wrong shape detected" || bad "wrong shape not detected"

plan_ai tests/opt/transpose.c $FX/unknown_region.json
[ "$(q $TMP/r.json "sum(1 for c in d['optimization']['candidates'] if c['status']=='invalid')")" = 2 ] && ok "invented / missing regions rejected as invalid" || bad "invalid regions"
q $TMP/r.json "[c['reason'] for c in d['optimization']['candidates'] if c['status']=='invalid']" | grep -q "unknown region" && ok "the reason names the unknown region" || bad "invalid reason"

plan_ai tests/opt/transpose.c $FX/duplicates.json
[ "$(q $TMP/r.json "d['optimization']['ai']['proposals']['duplicate']")" = 2 ] && ok "duplicate proposals (even re-spelled) detected" || bad "duplicates: $(q $TMP/r.json "d['optimization']['ai']['proposals']")"
[ "$(q $TMP/r.json "sum(1 for c in d['optimization']['candidates'] if c['source']=='model' and c['transformation']=='interchange' and c['status']=='found')")" = 1 ] && ok "duplicates are not tested twice" || bad "duplicates evaluated twice"

plan_ai tests/opt/transpose.c $FX/unsupported_only.json
[ "$(q $TMP/r.json "d['optimization']['ai']['proposals']['unsupported']")" = 3 ] && ok "tiling, parallelization and an invented name: all reported unsupported" || bad "unsupported count"
[ "$(q $TMP/r.json "d['optimization']['applied']")" = 0 ] && ok "nothing is claimed applied for an unsupported proposal" || bad "applied unsupported"

plan_ai tests/opt/transpose.c $FX/fenced.json
[ "$(q $TMP/r.json "d['optimization']['ai']['proposals']['total']")" = 1 ] && ok "JSON inside a Markdown fence is extracted" || bad "fenced"

plan_ai tests/opt/illegal_interchange.c $FX/unsafe_interchange.json
q $TMP/r.json "[c['reason'] for c in d['optimization']['candidates'] if c['source']=='model'][0]" | grep -q "not provably safe" && ok "unsafe proposal refused by the dependence analysis, with the reason" || bad "unsafe not refused" "$(q $TMP/r.json "[c for c in d['optimization']['candidates'] if c['source']=='model']")"

plan_ai tests/opt/unroll_licm.c $FX/valid_unroll_licm.json
[ "$(q $TMP/r.json "sorted(set(c['transformation'] for c in d['optimization']['candidates'] if c['source']=='model'))")" = "['interchange', 'licm', 'strength_reduction', 'unrolling']" ] && ok "new transformations (licm, unrolling) are reachable from proposals" || bad "licm/unrolling not attached" "$(q $TMP/r.json "[(c['transformation'],c['status']) for c in d['optimization']['candidates'] if c['source']=='model']")"
[ "$(q $TMP/r.json "[c['status'] for c in d['optimization']['candidates'] if c['transformation']=='strength_reduction'][0]")" = unsupported ] && ok "strength reduction reported unsupported" || bad "strength reduction"

plan_ai tests/opt/transpose.c $FX/valid_transpose.json --llm-max-reply 100
[ "$RC" = 3 ] && ok "oversized reply: refused (exit 3)" || bad "oversized reply: exit $RC"
has "limit" $TMP/r.err && ok "oversized reply: the limit is named" || bad "oversized message"
[ ! -f $TMP/r.json ] && ok "oversized reply: no output files were written" || bad "oversized: output written"

echo "== B. hostile and wrong replies through the whole pipeline (MOCKED reply, real compile/test/time) =="
if command -v gcc >/dev/null 2>&1; then
    rm -f /tmp/HT_COMPASS_PWNED
    $HTC tests/opt/transpose.c --optimize --no-bench --llm-reply-file $FX/hostile.json -o "$TMP/h.c" --json "$TMP/h.json" --topology "$TOPO" > "$TMP/h.txt" 2> "$TMP/h.err"
    [ "$?" = 0 ] && ok "hostile reply (embedded C code): handled" || bad "hostile: failed"
    [ ! -e /tmp/HT_COMPASS_PWNED ] && ok "model-supplied code was never executed" || bad "model-supplied code RAN"
    ! grep -q "PWNED\|return 99" "$TMP/h.c" && ok "model-supplied code never reached the generated program" || bad "model code in output"
    gcc -O1 -pthread -w -o "$TMP/h_bin" "$TMP/h.c" 2>/dev/null && [ "$("$TMP/h_bin")" = "10.000000" ] && ok "the generated program still computes the right answer" || bad "hostile: wrong program"

    HT_TEST_FORCE_UNSAFE=1 $HTC tests/opt/illegal_interchange.c --optimize --no-bench --llm-reply-file $FX/unsafe_interchange.json \
        -o "$TMP/f.c" --json "$TMP/f.json" --topology "$TOPO" > "$TMP/f.txt" 2> "$TMP/f.err"
    has "DIFFERENTIAL TEST FAILED" "$TMP/f.txt" && ok "an incorrect transformation (forced past the proof) is rejected by differential testing" || bad "differential test did not catch it"
    [ "$(q $TMP/f.json "sum(1 for c in d['optimization']['candidates'] if c['transformation']=='interchange' and c['status']=='applied')")" = 0 ] && ok "the incorrect interchange was not applied" || bad "incorrect interchange applied"

    $HTC tests/opt/unroll_licm.c --optimize --no-bench --llm-reply-file $FX/valid_unroll_licm.json -o "$TMP/u.c" --json "$TMP/u.json" --topology "$TOPO" > "$TMP/u.txt" 2> "$TMP/u.err"
    [ "$(q $TMP/u.json "any(c['status']=='applied' for c in d['optimization']['candidates'] if c['source']=='model' and c['transformation']=='licm')")" = True ] && ok "a model-proposed transformation passes every check and is applied" || bad "licm proposal not applied" "$(q $TMP/u.json "[(c['source'],c['transformation'],c['status'],c.get('reason','')[:60]) for c in d['optimization']['candidates']]")"
    [ "$(q $TMP/u.json "all(c['differential_test']['passed'] for c in d['optimization']['candidates'] if c['status']=='applied')")" = True ] && ok "every applied change has a passing differential test" || bad "applied without test"
else
    echo "  (gcc not found: skipping section B)"
fi

echo "== C. the service is not usable (nothing may be faked) =="
rm -f $TMP/c.c $TMP/c.json
$HTC tests/opt/transpose.c --optimize --llm-host 127.0.0.1:9 --llm-no-autostart -o $TMP/c.c --json $TMP/c.json --topology "$TOPO" > $TMP/c.out 2> $TMP/c.err
[ "$?" = 3 ] && ok "--optimize with no model service: exit 3" || bad "no service: wrong exit"
has "no AI analysis was performed" $TMP/c.err && ok "the error says plainly that no AI analysis happened" || bad "no clear error"
has "offline-optimize" $TMP/c.err && ok "the error offers the offline mode" || bad "no offline hint"
[ ! -e $TMP/c.c ] && [ ! -e $TMP/c.json ] && ok "no output files are written when the AI could not run" || bad "output written"

$HTC tests/opt/transpose.c --offline-optimize --no-bench --llm-host 127.0.0.1:9 -o $TMP/o.c --json $TMP/o.json --topology "$TOPO" > $TMP/o.out 2> $TMP/o.err
[ "$?" = 0 ] && ok "--offline-optimize works with no model service" || bad "offline failed"
[ "$(q $TMP/o.json "d['optimization']['ai']['mode']")" = offline-rules ] && ok "offline mode is labelled as such in the report" || bad "offline label"
[ "$(q $TMP/o.json "d['optimization']['ai']['available']")" = False ] && ok "offline mode does not claim an AI was available" || bad "offline claims AI"
$HTC tests/opt/transpose.c --opt-plan -o $TMP/p.c --json $TMP/p.json --topology "$TOPO" > /dev/null 2>&1 && ok "--opt-plan (no AI) still works" || bad "--opt-plan failed"

$HTC tests/opt/transpose.c --optimize --llm-host 8.8.8.8:11434 -o $TMP/n.c --json $TMP/n.json > /dev/null 2> $TMP/n.err
[ "$?" = 1 ] && has "this machine" $TMP/n.err && ok "a non-loopback model address is refused" || bad "remote host accepted"

$HTC --llm-host 127.0.0.1:9 --llm-no-autostart --llm-status > $TMP/s.json 2>/dev/null
[ "$(q $TMP/s.json "d['ready']")" = False ] && ok "--llm-status reports not ready" || bad "status"

echo "== D. Ollama API behaviour (MOCK server replaying real Ollama response formats) =="
mock() {   # mock SCENARIO REPLY MODELS  -> sets MOCK_PORT
    [ -n "$MOCK_PID" ] && { kill $MOCK_PID 2>/dev/null; wait $MOCK_PID 2>/dev/null; }
    python3 tests/mock_ollama.py --scenario "$1" --reply "$2" --models "$3" > $TMP/mock.out &
    MOCK_PID=$!
    local i
    for i in $(seq 1 50); do grep -q PORT $TMP/mock.out 2>/dev/null && break; sleep 0.1; done
    MOCK_PORT=$(awk '{print $2}' $TMP/mock.out)
}
via_mock() {   # via_mock [extra args]  (transpose, plan mode, real HTTP path)
    rm -f $TMP/m.json
    $HTC tests/opt/transpose.c --opt-plan --ai --llm-host 127.0.0.1:$MOCK_PORT --llm-no-autostart -o $TMP/m.c --json $TMP/m.json --topology "$TOPO" "$@" > $TMP/m.out 2> $TMP/m.err
    RC=$?
}

mock ok $FX/valid_transpose.json "mock-coder:3b"
via_mock
[ "$RC" = 0 ] && [ "$(q $TMP/m.json "d['optimization']['ai']['mode']")" = local-model ] && ok "HTTP path: a model is consulted over the Ollama API" || bad "http ok" "$(head -3 $TMP/m.err)"
[ "$(q $TMP/m.json "d['optimization']['ai']['model']")" = "mock-coder:3b" ] && ok "the model is chosen from the installed list" || bad "model pick"
[ "$(q $TMP/m.json "d['optimization']['ai']['response']['generated_tokens']")" = 22 ] && ok "token counts come from the Ollama reply" || bad "tokens"
via_mock --llm-model other:1b
[ "$RC" = 3 ] && has "ollama pull other:1b" $TMP/m.err && ok "requested model not installed: exit 3 with the exact pull command" || bad "missing model" "$(head -4 $TMP/m.err)"

mock ok $FX/valid_transpose.json ""
via_mock
[ "$RC" = 3 ] && has "no model is installed" $TMP/m.err && ok "service running but no model installed: exit 3 with setup instructions" || bad "no models"

mock chunked $FX/valid_transpose.json "mock-coder:3b"
via_mock
[ "$RC" = 0 ] && [ "$(q $TMP/m.json "d['optimization']['ai']['proposals']['total']")" = 2 ] && ok "chunked transfer encoding is decoded" || bad "chunked"

mock truncated $FX/truncated.json "mock-coder:3b"
via_mock
[ "$RC" = 0 ] && [ "$(q $TMP/m.json "d['optimization']['ai']['response']['cut_off_at_token_limit']")" = True ] && ok "a reply cut off at the token limit is detected and reported" || bad "truncation flag"

mock slow $FX/valid_transpose.json "mock-coder:3b"
via_mock --llm-timeout 2
[ "$RC" = 3 ] && has "llm-timeout" $TMP/m.err && ok "model timeout: exit 3, with advice" || bad "timeout" "$(head -3 $TMP/m.err)"

mock huge $FX/valid_transpose.json "mock-coder:3b"
via_mock --llm-max-reply 50000
[ "$RC" = 3 ] && has "limit" $TMP/m.err && ok "oversized model reply over HTTP: discarded, exit 3" || bad "huge" "$(head -3 $TMP/m.err)"

mock garbage $FX/valid_transpose.json "mock-coder:3b"
via_mock
[ "$RC" = 3 ] && has "not valid JSON" $TMP/m.err && ok "a server that does not speak JSON is reported" || bad "garbage" "$(head -3 $TMP/m.err)"

mock http500 $FX/valid_transpose.json "mock-coder:3b"
via_mock
[ "$RC" = 3 ] && has "HTTP 500" $TMP/m.err && ok "HTTP 500 from the model service is reported" || bad "http500"

[ -n "$MOCK_PID" ] && kill $MOCK_PID 2>/dev/null

echo
echo "mocked AI tests - passed: $pass   failed: $fail"
[ "$fail" = 0 ]
