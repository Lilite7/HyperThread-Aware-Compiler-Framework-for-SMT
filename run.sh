#!/usr/bin/env bash
# HT-Compass launcher.   ./run.sh [dashboard|demo|optimize|optimize-offline|llm-setup|llm-status|llm-test|test|fuzz|build|help]
#
#   dashboard   (default) build, start the dashboard, open it in your browser
#   demo        build, analyse tests/mixed.c, build and run the generated program
#   optimize    build, optimize tests/opt/threads_walk.c with the LOCAL language model (needs Ollama + a model)
#   optimize-offline   the same without any AI (the compiler's own rules)
#   llm-setup   pick a model for this machine, install it with Ollama (internet needed once)
#   llm-status  is the local model ready?
#   llm-test    REAL-inference test against the installed model (skips if there is none)
#   test        build and run every test
#   fuzz        attack the dependence analysis with random loop nests (needs python3)
#   build       just build ./htcompiler
set -eu
cd "$(dirname "$0")"

PORT="${PORT:-8765}"
need() { command -v "$1" >/dev/null 2>&1; }
die()  { echo "error: $*" >&2; exit 1; }

build() {
    need gcc  || die "gcc not found. Install it first, e.g.  sudo apt install build-essential"
    need make || die "make not found. Install it first, e.g.  sudo apt install build-essential"
    if need bison && need flex && [ -z "${HT_FORCE_GENERATED:-}" ]; then
        make -s
    else
        echo "(flex/bison not installed - using the pre-generated parser in generated/)"
        make -s USE_GENERATED=1
    fi
    [ -x ./htcompiler ] || die "build failed"
}

open_url() {
    if   need xdg-open;  then xdg-open "$1" >/dev/null 2>&1 || true
    elif need wslview;   then wslview "$1" >/dev/null 2>&1 || true
    elif need open;      then open "$1" >/dev/null 2>&1 || true
    elif need cmd.exe;   then cmd.exe /c start "" "$1" >/dev/null 2>&1 || true
    fi
}

cmd="${1:-dashboard}"
case "$cmd" in
    build)
        build
        echo "Built ./htcompiler"
        ;;
    test)
        build
        ./tests/run_tests.sh
        if need python3; then python3 frontend/test_server.py 2>&1 | tail -3; fi
        ;;
    optimize)
        build
        need gcc || die "gcc is needed to test and time optimizations"
        mkdir -p out
        echo "== optimizing tests/opt/threads_walk.c: the local model proposes, the compiler checks every proposal"
        echo "   (first run starts Ollama; on a CPU-only machine the model may need a minute or more)"
        ./htcompiler tests/opt/threads_walk.c --optimize -o out/program_gen.c --json out/program.htmeta.json \
            --topology "0,4;1,5;2,6;3,7" --emit-opt out/program_opt.c || {
            echo; echo "The AI step failed (see above). Without a model you can still run:  ./run.sh optimize-offline"; exit 1; }
        echo
        echo "Files in ./out :  program_opt.c (optimized)   program_gen.c (optimized + thread pinning)"
        echo "                  program.htmeta.json (drop it on the dashboard to see every proposal and decision)"
        ;;
    optimize-offline)
        build
        need gcc || die "gcc is needed to test and time optimizations"
        mkdir -p out
        echo "== optimizing tests/opt/threads_walk.c with the compiler's own rules (no AI)"
        ./htcompiler tests/opt/threads_walk.c --offline-optimize -o out/program_gen.c --json out/program.htmeta.json \
            --topology "0,4;1,5;2,6;3,7" --emit-opt out/program_opt.c
        echo
        echo "Files in ./out :  program_opt.c   program_gen.c   program.htmeta.json"
        ;;
    llm-setup)
        build
        exec ./tools/llm_setup.sh "${@:2}"
        ;;
    llm-status)
        build
        ./htcompiler --llm-status
        ;;
    llm-test)
        build
        exec ./tests/llm_integration.sh "${@:2}"
        ;;
    fuzz)
        need python3 || die "python3 is needed for the fuzz test"
        build
        python3 tests/fuzz_opt.py "${N:-1000}" "${SEED:-1}"
        ;;
    demo)
        build
        mkdir -p out
        echo "== analysing tests/mixed.c for a 4-core, 8-thread (SMT) machine"
        ./htcompiler tests/mixed.c -o out/program_gen.c --json out/program.htmeta.json \
            --topology "0,4;1,5;2,6;3,7"
        echo
        echo "== building and running the generated program (pins threads when those CPUs exist)"
        gcc -O2 -pthread -o out/program_gen out/program_gen.c -lm
        ./out/program_gen
        echo
        echo "Files in ./out :  program_gen.c   program.htmeta.json   program_gen"
        echo "View the report:  ./run.sh   then drop out/program.htmeta.json on the page"
        ;;
    dashboard)
        build
        if ! need python3; then
            echo "python3 not found, so 'Analyze code' is unavailable."
            echo "Open frontend/index.html in your browser and drop a report on it."
            open_url "file://$PWD/frontend/index.html"
            exit 0
        fi
        url="http://127.0.0.1:$PORT"
        echo "Dashboard: $url      (Ctrl+C to stop)"
        ( sleep 1; open_url "$url" ) &
        exec python3 frontend/server.py --port "$PORT"
        ;;
    help|-h|--help)
        sed -n '2,13p' "$0" | sed 's/^# \{0,1\}//'
        ;;
    *)
        die "unknown command '$cmd' (try ./run.sh help)"
        ;;
esac
