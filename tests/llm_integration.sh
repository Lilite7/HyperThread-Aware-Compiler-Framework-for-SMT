#!/usr/bin/env bash
# REAL-INFERENCE integration test.  Needs Ollama and an installed model; downloads nothing.
#
#   ./tests/llm_integration.sh [program.c]
#
# It runs  ./htcompiler PROGRAM --optimize  against the locally installed model and checks that a
# genuine reply went through the compiler's validation pipeline:
#   - the compiler started/used the local Ollama service by itself (no manual prompt, no API key)
#   - a real reply came back (characters, tokens, time are recorded)
#   - every proposal was classified (supported / unsupported / invalid / duplicate)
#   - nothing was applied unless it passed the differential test
#
# Exit status:  0 = ran and passed   1 = ran and FAILED   77 = SKIPPED (no model available; nothing was tested)
set -u
cd "$(dirname "$0")/.."
PROG=${1:-tests/opt/transpose.c}
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
q() { python3 - "$1" "$2" <<'PY'
import json, sys
d = json.load(open(sys.argv[1]))
print(eval(sys.argv[2]))
PY
}

command -v python3 >/dev/null 2>&1 || { echo "SKIPPED: python3 is needed to read the JSON report"; exit 77; }
[ -x ./htcompiler ] || { echo "build first: make"; exit 1; }

./htcompiler --llm-status > "$TMP/status.json" 2>/dev/null
installed=$(q "$TMP/status.json" "d['service']['ollama_installed']")
reachable=$(q "$TMP/status.json" "d['service']['reachable']")
if [ "$installed" != True ] && [ "$reachable" != True ]; then
    echo "SKIPPED (nothing was tested): $(q "$TMP/status.json" "d['message']")"
    echo "  Set up with:  ./run.sh llm-setup"
    exit 77
fi

echo "== real local-model integration test on $PROG =="
echo "service: $(q "$TMP/status.json" "d['message']")"
start=$(date +%s)
./htcompiler "$PROG" --optimize --no-bench --llm-dump-prompt "$TMP/prompt.txt" \
    -o "$TMP/out.c" --json "$TMP/out.json" --topology "0,4;1,5" > "$TMP/out.txt" 2> "$TMP/err.txt"
rc=$?
secs=$(( $(date +%s) - start ))
if [ "$rc" = 3 ]; then
    echo "SKIPPED (the model could not be used, so nothing was tested):"
    sed 's/^/  /' "$TMP/err.txt" | head -6
    exit 77
fi
[ "$rc" = 0 ] || { echo "FAILED: htcompiler exited with $rc"; head -5 "$TMP/err.txt"; exit 1; }

fail=0
check() { if [ "$2" = True ]; then echo "  PASS  $1"; else echo "  FAIL  $1"; fail=1; fi; }
J="$TMP/out.json"
check "the report says a local model was used (not a test reply file)"      "$(q $J "d['optimization']['ai']['mode']=='local-model'")"
check "a model name is recorded and it is one of the installed models"     "$(q $J "d['optimization']['ai']['model'] in d['optimization']['ai']['installed_models']")"
check "a real reply came back (characters > 0)"                            "$(q $J "d['optimization']['ai']['response']['characters']>0")"
check "token counts and timing come from Ollama"                           "$(q $J "d['optimization']['ai']['response']['generated_tokens']>0 and d['optimization']['ai']['response']['seconds']>0")"
check "the raw reply is kept for inspection"                               "$(q $J "len(d['optimization']['ai']['response']['excerpt'])>0")"
check "the prompt really contained the program's stable region ids"        "$(grep -q 'main/L1\|/L1' "$TMP/prompt.txt" && echo True || echo False)"
check "every proposal was classified (none silently dropped)"              "$(q $J "(lambda p: p['total']==p['supported']+p['unsupported']+p['invalid']+p['duplicate'])(d['optimization']['ai']['proposals'])")"
check "every applied change has a passing differential test"               "$(q $J "all(c['differential_test']['passed'] for c in d['optimization']['candidates'] if c['status']=='applied')")"
check "nothing the model said was applied without the compiler's checks"   "$(q $J "all(c['legality_evidence'] and c['differential_test']['passed'] for c in d['optimization']['candidates'] if c['status']=='applied' and c['source']=='model')")"

echo
echo "--- what the real model did ---"
python3 - "$J" <<'PY'
import json, sys
o = json.load(open(sys.argv[1]))['optimization']; a = o['ai']
print("model            :", a['model'], "(Ollama %s, %s)" % (a.get('ollama_version'), a['endpoint']))
print("service note     :", a.get('service_note') or "-")
print("prompt           : %d characters, %d tokens" % (a['request']['prompt_characters'], a['response']['prompt_tokens']))
print("reply            : %d characters, %d tokens, %.1f s%s" % (a['response']['characters'], a['response']['generated_tokens'], a['response']['seconds'], ", CUT OFF at the token limit" if a['response']['cut_off_at_token_limit'] else ""))
print("raw reply excerpt:", repr(a['response']['excerpt'][:300]))
print("proposals        :", a['proposals'])
for n in a['validation_notes']: print("validation note  :", n)
print("candidates:")
for c in o['candidates']:
    print("  [%-8s] %-11s %-12s %-9s %s" % (c['source'], c['status'], c['transformation'], c.get('region','-'), (c.get('reason') or '')[:70]))
PY
echo
echo "total time: ${secs} s"
if [ "$fail" = 0 ]; then echo "REAL-INFERENCE INTEGRATION TEST: PASSED"; exit 0; else echo "REAL-INFERENCE INTEGRATION TEST: FAILED"; exit 1; fi
