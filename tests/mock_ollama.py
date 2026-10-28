#!/usr/bin/env python3
"""MOCK of Ollama's HTTP API, for TESTS ONLY.

This is NOT a language model and performs no inference.  It replays canned text so the
transport, error handling and validation paths of htcompiler can be tested without a model.
The response formats are copied from a real Ollama 0.40.10 server:

    GET  /api/version -> {"version":"0.40.10"}
    GET  /api/tags    -> {"models":[{"name":"..."}]}
    POST /api/chat    -> {"message":{"role":"assistant","content":"..."},"done":true,"done_reason":"stop",...}
    unknown model     -> HTTP 404 {"error":"model 'x' not found"}

    mock_ollama.py --scenario ok|slow|huge|chunked|truncated|garbage|http500 [--reply FILE] [--models a,b]
Prints "PORT <n>" once it is listening.
"""
import argparse
import http.server
import json
import sys
import time

args = None
REPLY = ""


class H(http.server.BaseHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def _send(self, code, body, chunked=False, ctype="application/json; charset=utf-8"):
        data = body if isinstance(body, bytes) else body.encode()
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        if chunked:
            self.send_header("Transfer-Encoding", "chunked")
            self.end_headers()
            for i in range(0, len(data), 37):
                piece = data[i:i + 37]
                self.wfile.write(b"%x\r\n" % len(piece) + piece + b"\r\n")
            self.wfile.write(b"0\r\n\r\n")
        else:
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)

    def do_GET(self):
        if self.path == "/api/version":
            self._send(200, json.dumps({"version": "0.0.0-mock"}))
        elif self.path == "/api/tags":
            models = [m for m in args.models.split(",") if m]
            self._send(200, json.dumps({"models": [{"name": m, "size": 1} for m in models]}))
        else:
            self._send(404, json.dumps({"error": "not found"}))

    def do_POST(self):
        n = int(self.headers.get("Content-Length", "0"))
        req = json.loads(self.rfile.read(n) or b"{}")
        if self.path != "/api/chat":
            return self._send(404, json.dumps({"error": "not found"}))
        models = [m for m in args.models.split(",") if m]
        want = req.get("model", "")
        if want not in models and (":" in want or want + ":latest" not in models):
            return self._send(404, json.dumps({"error": "model '%s' not found" % want}))
        sc = args.scenario
        if sc == "http500":
            return self._send(500, json.dumps({"error": "boom"}))
        if sc == "garbage":
            return self._send(200, "this is not json at all", ctype="text/plain")
        if sc == "slow":
            time.sleep(15)
        content = REPLY
        if sc == "huge":
            content = "x" * 400000
        reply = {"model": want, "created_at": "2026-01-01T00:00:00Z",
                 "message": {"role": "assistant", "content": content},
                 "done": True, "done_reason": "length" if sc == "truncated" else "stop",
                 "prompt_eval_count": 11, "eval_count": 22}
        self._send(200, json.dumps(reply), chunked=(sc == "chunked"))


def main():
    global args, REPLY
    ap = argparse.ArgumentParser()
    ap.add_argument("--scenario", default="ok")
    ap.add_argument("--reply", default="")
    ap.add_argument("--models", default="mock-coder:3b")
    args = ap.parse_args()
    if args.reply:
        REPLY = open(args.reply).read()
    srv = http.server.ThreadingHTTPServer(("127.0.0.1", 0), H)
    print("PORT %d" % srv.server_address[1], flush=True)
    srv.serve_forever()


if __name__ == "__main__":
    main()
