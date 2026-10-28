#!/usr/bin/env bash
# Set up the LOCAL language model that  ./htcompiler prog.c --optimize  consults.
#
#   ./tools/llm_setup.sh              pick a model for this machine, pull it, verify it
#   ./tools/llm_setup.sh --dry-run    only show what it would do
#   MODEL=qwen2.5-coder:7b ./tools/llm_setup.sh     choose the model yourself
#
# Free: no API key, no account, no per-request charge.  The model download needs an internet
# connection ONCE; after that inference runs offline.  Running a model uses your CPU/GPU, RAM and
# electricity (a few minutes of full CPU load per optimization on a laptop without a GPU).
set -u
cd "$(dirname "$0")/.."
DRY=0; [ "${1:-}" = "--dry-run" ] && DRY=1

# ---------------------------------------------------------------- hardware
ram_gb() {
    if [ -r /proc/meminfo ]; then awk '/MemTotal/ {printf "%d", $2/1048576}' /proc/meminfo
    elif command -v sysctl >/dev/null 2>&1; then sysctl -n hw.memsize 2>/dev/null | awk '{printf "%d", $1/1073741824}'
    else echo 0; fi
}
vram_gb() {
    command -v nvidia-smi >/dev/null 2>&1 || { echo 0; return; }
    nvidia-smi --query-gpu=memory.total --format=csv,noheader,nounits 2>/dev/null | sort -n | tail -1 | awk '{printf "%d", $1/1024}'
}
RAM=$(ram_gb); VRAM=$(vram_gb); CORES=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 1)
APPLE=0; [ "$(uname -s)" = Darwin ] && [ "$(uname -m)" = arm64 ] && APPLE=1

echo "This machine: ${RAM} GB RAM, ${CORES} CPU thread(s), NVIDIA GPU memory: ${VRAM} GB$( [ $APPLE = 1 ] && echo ', Apple silicon (unified memory)')"

# ---------------------------------------------------------------- choose a model
# Size rule of thumb for 4-bit models: weights ~0.6 GB per billion parameters, plus working memory.
if [ -n "${MODEL:-}" ]; then WHY="chosen by you"
elif [ "$VRAM" -ge 8 ] || [ "$RAM" -ge 16 ]; then MODEL=qwen2.5-coder:7b;  WHY="enough memory for a 7B model (about 4.7 GB download, 6-8 GB RAM)"
elif [ "$RAM" -ge 8 ];                          then MODEL=qwen2.5-coder:3b;  WHY="a 3B model fits comfortably (about 1.9 GB download, 4 GB RAM)"
elif [ "$RAM" -ge 4 ];                          then MODEL=qwen2.5-coder:1.5b; WHY="limited RAM: a 1.5B model (about 1 GB download); its proposals will be less reliable"
else                                                 MODEL=qwen2.5-coder:0.5b; WHY="very little RAM: a 0.5B model; expect poor proposals (the compiler still checks everything)"
fi
echo "Selected model: $MODEL   ($WHY)"
[ "$VRAM" -lt 4 ] && [ "$APPLE" = 0 ] && echo "No usable GPU: inference runs on the CPU. Expect one optimization consultation to take from tens of seconds to a few minutes."

# ---------------------------------------------------------------- Ollama
if ! command -v ollama >/dev/null 2>&1; then
    echo
    echo "Ollama is not installed.  Install it (free, open source), then run this script again:"
    case "$(uname -s)" in
        Darwin) echo "    brew install ollama        # or download from https://ollama.com/download" ;;
        Linux)  echo "    curl -fsSL https://ollama.com/install.sh | sh        # read the script first if you prefer"
                echo "    # or download the tarball from https://github.com/ollama/ollama/releases" ;;
        *)      echo "    download the installer from https://ollama.com/download   (on Windows, use WSL for HT-Compass)" ;;
    esac
    [ "$DRY" = 1 ] && exit 0
    exit 1
fi
echo "Ollama: $(ollama --version 2>&1 | grep -i version | head -1)"

echo
echo "Plan:"
echo "    ollama serve                    (HT-Compass starts this by itself when needed)"
echo "    ollama pull $MODEL      (downloads once; needs internet only now)"
echo "    ./htcompiler --llm-status       (verify)"
[ "$DRY" = 1 ] && { echo "(dry run: nothing was changed)"; exit 0; }

# start the service if it is not answering
if ! ./htcompiler --llm-status >/dev/null 2>&1 && ! curl -s -m 2 "http://${OLLAMA_HOST:-127.0.0.1:11434}/api/version" >/dev/null 2>&1; then
    echo "Starting 'ollama serve' in the background (log: /tmp/htcompass_ollama.log) ..."
    (setsid nohup ollama serve > /tmp/htcompass_ollama.log 2>&1 &)
    for i in $(seq 1 30); do curl -s -m 1 "http://${OLLAMA_HOST:-127.0.0.1:11434}/api/version" >/dev/null 2>&1 && break; sleep 1; done
fi

if ollama list 2>/dev/null | awk '{print $1}' | grep -qx "$MODEL"; then
    echo "$MODEL is already installed."
else
    echo "Pulling $MODEL (this is the one step that needs the internet) ..."
    ollama pull "$MODEL" || { echo "The pull failed. If the name was not found, see https://ollama.com/library/qwen2.5-coder for current tags and re-run with MODEL=<tag>."; exit 1; }
fi

echo
./htcompiler --llm-status
echo
echo "Done. Try:   ./htcompiler tests/opt/transpose.c --optimize"
