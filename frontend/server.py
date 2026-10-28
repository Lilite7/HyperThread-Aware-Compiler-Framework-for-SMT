#!/usr/bin/env python3
"""Local server for the HT-Compass dashboard.

    python3 frontend/server.py            # then open http://127.0.0.1:8765

Serves frontend/index.html and one API endpoint that runs ./htcompiler:

    GET  /api/health    -> {"ok": true, "compiler": "htcompiler 1.0.0"}
    POST /api/analyze   -> {"ok": true, "report": {...}, "generated": "..."}
                           {"ok": false, "error": "<compiler diagnostics>"}
                           optional fields: optimize, bench, fp_reassoc (booleans), suggestions (text)
    POST /api/prompt    -> {"ok": true, "prompt": "..."}   a prompt for any chat AI (no API involved)
    GET  /api/llm/status-> state of the LOCAL language model service (Ollama), from `htcompiler --llm-status`

With "optimize": true the compiler consults the local Ollama model automatically (like the CLI's
--optimize).  Pass "ai": false for the rule-based offline mode (--offline-optimize).  If the model
cannot be used the compiler stops with an error and the response carries "ai_unavailable": true;
nothing is simulated.

NOTE: with "optimize": true the compiler builds and RUNS the submitted program (many times) to test and
time the optimizations.  The program can only use the mini-C subset, and every run has a wall-clock
limit and an address-space limit, but it is still your code running on your machine.

Only the Python standard library is used. The server listens on 127.0.0.1,
checks the Host/Origin headers, passes only validated options to the compiler
(no shell), and runs it in a temporary directory with a time limit. The
compiler itself only parses and analyses the program; it never runs it.
"""
import argparse
import json
import os
import re
import subprocess
import tempfile
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

HERE = os.path.dirname(os.path.abspath(__file__))
PROJECT = os.path.dirname(HERE)
COMPILER = os.path.join(PROJECT, "htcompiler")
INDEX = os.path.join(HERE, "index.html")

MAX_BODY = 256 * 1024
TIMEOUT_S = 20
OPT_TIMEOUT_S = 300                 # whole optimization run (builds + many test runs)
AI_TIMEOUT_S = 1500                 # ... including a local language model answering on this machine
LLM_TIMEOUT_S = 400                 # one model answer
RUN_TIMEOUT_S = 20                  # one run of the program under test
TOPOLOGY_RE = re.compile(r"^\d{1,3}(,\d{1,3})?(;\d{1,3}(,\d{1,3})?)*$")
LOCAL_HOSTS = {"127.0.0.1", "localhost", "[::1]"}
MODEL_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._:/-]{0,79}$")
ADVICE_LINE_RE = re.compile(r"^\s*(#.*|[a-z]{3,16}\s+[A-Za-z_][A-Za-z0-9_]{0,40}\s+\d{1,6})?\s*$")


def compiler_version():
    try:
        out = subprocess.run([COMPILER, "--version"], capture_output=True, text=True, timeout=5)
        return out.stdout.strip()
    except (OSError, subprocess.SubprocessError):
        return ""


def flag(req, name):
    v = req.get(name, False)
    if not isinstance(v, bool):
        raise ValueError("%s must be true or false." % name)
    return v


def build_args(req, workdir, plan_prompt=False):
    """Validate the request and return (argv, error)."""
    source = req.get("source")
    if not isinstance(source, str) or not source.strip():
        return None, "No C source was provided."
    if len(source.encode("utf-8")) > MAX_BODY - 4096:
        return None, "The source is too large (limit about 250 KB)."
    policy = req.get("policy", "pack")
    strategy = req.get("strategy", "optimized")
    if policy not in ("pack", "spread"):
        return None, "Unknown policy."
    if strategy not in ("optimized", "naive", "worst"):
        return None, "Unknown mapping strategy."
    try:
        optimize = flag(req, "optimize")
        bench = flag(req, "bench") if "bench" in req else True
        fp_ok = flag(req, "fp_reassoc")
        use_ai = flag(req, "ai") if "ai" in req else True
        llm_only = flag(req, "llm_only")
    except ValueError as e:
        return None, str(e)

    src = os.path.join(workdir, "program.c")
    with open(src, "w", encoding="utf-8") as f:
        f.write(source)

    argv = [COMPILER, "program.c", "-o", "program_gen.c", "--json", "program.htmeta.json",
            "--policy", policy, "--strategy", strategy, "--quiet"]
    if plan_prompt:
        argv += ["--export-prompt", "prompt.txt"]
    elif optimize:
        argv += ["--optimize" if use_ai else "--offline-optimize", "--run-timeout", str(RUN_TIMEOUT_S), "--opt-rounds", "3"]
        if use_ai:
            argv += ["--llm-timeout", str(LLM_TIMEOUT_S)]
            model = req.get("llm_model", "")
            if model:
                if not isinstance(model, str) or not MODEL_RE.match(model):
                    return None, "The model name may only contain letters, digits and . _ : / -"
                argv += ["--llm-model", model]
            if llm_only:
                argv.append("--llm-only")
        if not bench:
            argv.append("--no-bench")
        if fp_ok:
            argv.append("--fp-reassoc")
        advice = req.get("suggestions", "")
        if advice:
            if not isinstance(advice, str) or len(advice) > 4000:
                return None, "The advice text is too long."
            lines = advice.splitlines()
            if len(lines) > 60 or not all(ADVICE_LINE_RE.match(l) for l in lines):
                return None, "Advice lines must look like: interchange function_name 46"
            with open(os.path.join(workdir, "advice.txt"), "w", encoding="utf-8") as f:
                f.write("\n".join(lines) + "\n")
            argv += ["--suggest", "advice.txt"]
    topo = str(req.get("topology") or "").strip()
    if topo:
        if not TOPOLOGY_RE.match(topo):
            return None, "Topology must look like 0,4;1,5;2,6;3,7."
        argv += ["--topology", topo]
    cores = str(req.get("cores") or "").strip()
    if cores:
        if not cores.isdigit() or not 1 <= int(cores) <= 256:
            return None, "Cores must be a number from 1 to 256."
        argv += ["--cores", cores]
    return argv, None


class Handler(BaseHTTPRequestHandler):
    server_version = "HTCompassDashboard/1.0"

    def log_message(self, fmt, *args):
        print("  %s %s" % (self.command, self.path), "->", args[1] if len(args) > 1 else "")

    # ---- helpers ----
    def send_json(self, code, obj):
        body = json.dumps(obj).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def host_ok(self):
        host = self.headers.get("Host", "")
        name = host.rsplit(":", 1)[0] if not host.endswith("]") else host
        return name in LOCAL_HOSTS

    def origin_ok(self):
        origin = self.headers.get("Origin")
        if not origin:
            return True
        return origin.split("://", 1)[-1] == self.headers.get("Host", "")

    # ---- routes ----
    def do_GET(self):
        if not self.host_ok():
            return self.send_json(403, {"ok": False, "error": "Forbidden host."})
        if self.path in ("/", "/index.html"):
            try:
                with open(INDEX, "rb") as f:
                    body = f.read()
            except OSError:
                return self.send_json(500, {"ok": False, "error": "index.html is missing."})
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
        elif self.path == "/api/llm/status":
            try:
                p = subprocess.run([COMPILER, "--llm-status", "--llm-no-autostart"], capture_output=True, text=True, timeout=20)
                self.send_json(200, json.loads(p.stdout))
            except (OSError, ValueError, subprocess.SubprocessError):
                self.send_json(200, {"service": {"reachable": False, "ollama_installed": False, "version": "", "error": "htcompiler is not built"},
                                     "models": [], "selected_model": "", "ready": False,
                                     "message": "htcompiler is not built. Run make in the project folder."})
        elif self.path == "/api/health":
            version = compiler_version()
            if version:
                self.send_json(200, {"ok": True, "compiler": version})
            else:
                self.send_json(200, {"ok": False,
                                     "error": "htcompiler is not built. Run make in the project folder, then restart this server."})
        else:
            self.send_json(404, {"ok": False, "error": "Not found."})

    def read_request(self):
        try:
            length = int(self.headers.get("Content-Length", "0"))
        except ValueError:
            length = -1
        if length <= 0 or length > MAX_BODY:
            self.send_json(413, {"ok": False, "error": "Request body is empty or too large."})
            return None
        try:
            req = json.loads(self.rfile.read(length).decode("utf-8"))
            if not isinstance(req, dict):
                raise ValueError
        except (ValueError, UnicodeDecodeError):
            self.send_json(400, {"ok": False, "error": "The request is not valid JSON."})
            return None
        return req

    def do_POST(self):
        if not self.host_ok() or not self.origin_ok():
            return self.send_json(403, {"ok": False, "error": "Forbidden."})
        if self.path not in ("/api/analyze", "/api/prompt"):
            return self.send_json(404, {"ok": False, "error": "Not found."})
        req = self.read_request()
        if req is None:
            return
        if not compiler_version():
            return self.send_json(503, {"ok": False, "error": "htcompiler is not built. Run make first."})
        want_prompt = self.path == "/api/prompt"

        with tempfile.TemporaryDirectory(prefix="htc_") as work:
            argv, err = build_args(req, work, plan_prompt=want_prompt)
            if err:
                return self.send_json(400, {"ok": False, "error": err})
            optimizing = "--optimize" in argv or "--offline-optimize" in argv
            limit = (AI_TIMEOUT_S if "--optimize" in argv else OPT_TIMEOUT_S) if optimizing else TIMEOUT_S
            try:
                proc = subprocess.run(argv, cwd=work, capture_output=True, text=True, timeout=limit)
            except subprocess.TimeoutExpired:
                return self.send_json(200, {"ok": False, "error": "The compiler took longer than %d seconds." % limit})
            log = (proc.stderr or "").strip()
            if proc.returncode == 3:        # the local model could not be used: say so, fake nothing
                return self.send_json(200, {"ok": False, "ai_unavailable": True,
                                            "error": log or "The local language model could not be used."})
            if proc.returncode != 0:
                return self.send_json(200, {"ok": False, "error": log or proc.stdout.strip() or "The compiler failed."})
            if want_prompt:
                try:
                    with open(os.path.join(work, "prompt.txt"), encoding="utf-8") as f:
                        return self.send_json(200, {"ok": True, "prompt": f.read()})
                except OSError:
                    return self.send_json(500, {"ok": False, "error": "The compiler did not write a prompt."})
            try:
                with open(os.path.join(work, "program.htmeta.json"), encoding="utf-8") as f:
                    report = json.load(f)
                with open(os.path.join(work, "program_gen.c"), encoding="utf-8") as f:
                    generated = f.read()
            except (OSError, ValueError):
                return self.send_json(500, {"ok": False, "error": "The compiler did not produce its output files."})
            self.send_json(200, {"ok": True, "report": report, "generated": generated, "log": log})


def main():
    ap = argparse.ArgumentParser(description="HT-Compass dashboard server (localhost only)")
    ap.add_argument("--port", type=int, default=8765)
    args = ap.parse_args()
    if not os.path.exists(COMPILER):
        print("Note: %s does not exist yet. Run 'make' in the project folder." % COMPILER)
    srv = ThreadingHTTPServer(("127.0.0.1", args.port), Handler)
    print("HT-Compass dashboard: http://127.0.0.1:%d   (Ctrl+C to stop)" % args.port)
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        print("\nstopped")


if __name__ == "__main__":
    main()
