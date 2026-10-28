# HT-Compass quick start

## Run it (Linux, or Windows through WSL)

```bash
unzip ht-compass.zip
cd ht-compass
./run.sh
```

That builds the compiler and opens the dashboard at http://127.0.0.1:8765.
Press **Try the streaming and compute sample** to see a result straight away, or
**Analyze code** to paste your own C program.

| command | what it does |
|---|---|
| `./run.sh` | build, start the dashboard, open the browser |
| `./run.sh demo` | analyse `tests/mixed.c`, build and run the generated program, write everything to `out/` |
| `./run.sh optimize` | optimize a threaded example with the local model (needs Ollama + a model); writes `out/` |
| `./run.sh optimize-offline` | the same with the compiler's own rules, no AI |
| `./run.sh llm-setup` / `llm-status` / `llm-test` | set up, check, and really test the local model |
| `./run.sh test` | build and run all tests |
| `./run.sh fuzz` | attack the dependence analysis with thousands of random loop nests (needs python3) |
| `./run.sh build` | only build `./htcompiler` |

## Use the compiler directly

```bash
./htcompiler tests/mixed.c -o program_gen.c --json program.htmeta.json --topology "0,4;1,5;2,6;3,7"
gcc -O2 -pthread -o program_gen program_gen.c -lm
./program_gen
```

Then drop `program.htmeta.json` (and `program_gen.c`) on the dashboard.

## What you need

- **gcc** and **make** (`sudo apt install build-essential`).
- **python3**, only for the dashboard's *Analyze code* button and the fuzz test. Without it you can still open
  `frontend/index.html` and drop reports on it.
- flex and bison are **not** required; a pre-generated parser is included.

## Windows

The generated programs use Linux thread-pinning (`pthread_attr_setaffinity_np`), so use
**WSL** (Ubuntu): install it from the Microsoft Store, then `sudo apt install build-essential python3`
and run the commands above inside WSL. The dashboard also works from the Windows browser at the same address.

## Local AI optimization (optional, free)

`./htcompiler prog.c --optimize` asks a language model that runs **on your own computer** where to optimize. There is no API key,
no account and no per-request charge, and you never copy a prompt anywhere. The compiler proves, tests and times every suggestion
itself, so a poor suggestion is simply rejected.

```bash
# one time: install Ollama (free)         Linux:  curl -fsSL https://ollama.com/install.sh | sh
#                                         macOS:  brew install ollama
#                                         Windows: install WSL (Ubuntu), then use the Linux line inside WSL
./run.sh llm-setup        # looks at your RAM/GPU, picks a model, downloads it (internet needed once)
./run.sh llm-status       # should say ready

# every time
./htcompiler tests/opt/transpose.c --optimize          # starts Ollama by itself if needed
./run.sh optimize                                       # a worked example, writes out/
```

| if you see | do this |
|---|---|
| `AI optimization could not run ... Ollama is not installed` | install Ollama (above), then `./run.sh llm-setup` |
| `no model is installed` | `./run.sh llm-setup` |
| `did not answer ... --llm-timeout` | a CPU-only machine is slow: `--llm-timeout 600`, or a smaller model: `MODEL=qwen2.5-coder:1.5b ./run.sh llm-setup` |
| you just want to optimize without AI | `./htcompiler prog.c --offline-optimize` |

Notes: the first model download needs the internet, after that everything runs offline. Running a model uses your CPU/GPU, RAM and
electricity. `ollama serve` is started automatically and keeps running; stop it with `pkill ollama`. The dashboard (`./run.sh`)
has the same option in *Analyze code*, and shows the model's proposals and what the compiler did with each.

To check that a **real** model works end to end: `./run.sh llm-test` (it says SKIPPED, not passed, if no model is installed).

## If something goes wrong

| problem | fix |
|---|---|
| `gcc not found` / `make not found` | `sudo apt install build-essential` |
| port already in use | `PORT=9000 ./run.sh` |
| browser did not open | open http://127.0.0.1:8765 yourself |
| mapping shows a warning "host has no SMT" | normal on machines without Hyper-Threading; pass `--topology` to describe the machine your program will run on |
| generated code does not build on macOS | thread pinning is Linux-only; the compiler and dashboard still work |

More detail: `README.md` (usage and model) and `docs/DESIGN.md` (design notes and viva questions).
