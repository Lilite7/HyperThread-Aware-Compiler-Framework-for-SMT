#!/usr/bin/env python3
"""Tests for frontend/server.py (standard library only).

    python3 frontend/test_server.py        (needs ./htcompiler: run `make` first)
"""
import http.client
import importlib.util
import json
import os
import shutil
import subprocess
import sys
import threading
import time
import unittest
from http.server import ThreadingHTTPServer

HERE = os.path.dirname(os.path.abspath(__file__))
PROJECT = os.path.dirname(HERE)
spec = importlib.util.spec_from_file_location("htserver", os.path.join(HERE, "server.py"))
htserver = importlib.util.module_from_spec(spec)
spec.loader.exec_module(htserver)

MIXED = open(os.path.join(PROJECT, "tests", "mixed.c"), encoding="utf-8").read()
TOPO = "0,4;1,5;2,6;3,7"
COLUMN_WALK = """#include <stdio.h>
#define N 700
double a[N][N];
double b[N][N];
int main(void)
{
    int i;
    int j;
    for (i = 0; i < N; i++) {
        for (j = 0; j < N; j++) {
            a[i][j] = i + j * 0.5;
        }
    }
    for (j = 0; j < N; j++) {
        for (i = 0; i < N; i++) {
            b[i][j] = a[i][j] * 2.0;
        }
    }
    printf("%f\\n", b[3][4]);
    return 0;
}
"""


class ServerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not os.path.exists(htserver.COMPILER):
            raise unittest.SkipTest("htcompiler is not built; run make")
        cls.srv = ThreadingHTTPServer(("127.0.0.1", 0), htserver.Handler)
        cls.port = cls.srv.server_address[1]
        threading.Thread(target=cls.srv.serve_forever, daemon=True).start()

    @classmethod
    def tearDownClass(cls):
        cls.srv.shutdown()
        cls.srv.server_close()

    def call(self, method, path, body=None, headers=None, raw=None):
        conn = http.client.HTTPConnection("127.0.0.1", self.port, timeout=30)
        hdrs = dict(headers or {})
        data = raw
        if body is not None:
            data = json.dumps(body).encode()
            hdrs.setdefault("Content-Type", "application/json")
        conn.request(method, path, body=data, headers=hdrs)
        resp = conn.getresponse()
        payload = resp.read()
        conn.close()
        return resp.status, payload

    def analyze(self, **kw):
        status, payload = self.call("POST", "/api/analyze", body=kw)
        return status, json.loads(payload)

    def test_index_is_served(self):
        status, payload = self.call("GET", "/")
        self.assertEqual(status, 200)
        self.assertIn(b"HT-Compass", payload)

    def test_health(self):
        status, payload = self.call("GET", "/api/health")
        self.assertEqual(status, 200)
        self.assertTrue(json.loads(payload)["ok"])

    def test_analyze_success(self):
        status, out = self.analyze(source=MIXED, topology=TOPO)
        self.assertEqual(status, 200)
        self.assertTrue(out["ok"], out)
        rep = out["report"]
        self.assertEqual(len(rep["threads"]), 4)
        self.assertLess(rep["mapping"]["optimized_total"], rep["mapping"]["naive_total"])
        self.assertIn("__ht_create", out["generated"])
        self.assertIn("model", rep)

    def test_analyze_options(self):
        status, out = self.analyze(source=MIXED, topology=TOPO, policy="spread", cores="2", strategy="naive")
        self.assertTrue(out["ok"], out)
        self.assertEqual(out["report"]["mapping"]["policy"], "spread")
        self.assertEqual(out["report"]["mapping"]["strategy_emitted"], "naive")

    def test_compiler_error_is_reported(self):
        src = "int main(void)\n{\n    int x = 1;\n    switch (x) { }\n    return 0;\n}\n"
        status, out = self.analyze(source=src)
        self.assertEqual(status, 200)
        self.assertFalse(out["ok"])
        self.assertIn("Unsupported construct at line 4: switch statement", out["error"])

    def test_rejects_bad_options(self):
        for bad in ({"topology": "0,1; rm -rf /"}, {"policy": "nope"}, {"strategy": "x"}, {"cores": "abc"}, {"cores": "0"}):
            status, out = self.analyze(source=MIXED, **bad)
            self.assertEqual(status, 400, bad)
            self.assertFalse(out["ok"])

    def test_rejects_empty_source(self):
        status, out = self.analyze(source="   ")
        self.assertEqual(status, 400)

    def test_rejects_foreign_host_and_origin(self):
        status, _ = self.call("GET", "/api/health", headers={"Host": "evil.example.com"})
        self.assertEqual(status, 403)
        status, _ = self.call("POST", "/api/analyze", body={"source": MIXED},
                              headers={"Origin": "http://evil.example.com"})
        self.assertEqual(status, 403)

    def test_rejects_invalid_and_oversized_bodies(self):
        status, _ = self.call("POST", "/api/analyze", raw=b"not json", headers={"Content-Type": "application/json"})
        self.assertEqual(status, 400)
        status, _ = self.call("POST", "/api/analyze", raw=b"x" * (htserver.MAX_BODY + 10),
                              headers={"Content-Type": "application/json"})
        self.assertEqual(status, 413)

    @unittest.skipUnless(shutil.which("gcc"), "gcc is needed to test optimizations")
    def test_optimize_applies_and_verifies(self):
        status, out = self.analyze(source=COLUMN_WALK, topology=TOPO, optimize=True, ai=False, bench=False)
        self.assertEqual(status, 200)
        self.assertTrue(out["ok"], out)
        opt = out["report"]["optimization"]
        self.assertGreaterEqual(opt["applied"], 1)
        applied = [c for c in opt["candidates"] if c["status"] == "applied"]
        self.assertEqual(applied[0]["transformation"], "interchange")
        self.assertTrue(applied[0]["differential_test"]["passed"])
        self.assertIn("before", applied[0]["code"])

    def test_optimize_rejects_bad_flags(self):
        status, out = self.analyze(source=COLUMN_WALK, optimize="yes")
        self.assertEqual(status, 400)
        status, out = self.analyze(source=COLUMN_WALK, optimize=True, fp_reassoc=1)
        self.assertEqual(status, 400)

    @unittest.skipUnless(shutil.which("gcc"), "gcc is needed to test optimizations")
    def test_advice_is_validated_and_used(self):
        status, out = self.analyze(source=COLUMN_WALK, optimize=True, ai=False, bench=False,
                                   suggestions="interchange main 12; rm -rf /")
        self.assertEqual(status, 400)
        status, out = self.analyze(source=COLUMN_WALK, topology=TOPO, optimize=True, ai=False, bench=False,
                                   suggestions="# from an AI\ninterchange main 14\ninterchange nosuchfn 3\n")
        self.assertTrue(out["ok"], out)
        opt = out["report"]["optimization"]
        self.assertEqual(opt["advice"]["suggestions_given"], 2)
        self.assertEqual(opt["advice"]["matched_an_opportunity"], 1)
        self.assertTrue(any(c["suggested_by_advice"] and c["status"] == "rejected" for c in opt["candidates"]))

    def test_prompt_endpoint(self):
        status, payload = self.call("POST", "/api/prompt", body={"source": COLUMN_WALK})
        self.assertEqual(status, 200)
        out = json.loads(payload)
        self.assertTrue(out["ok"], out)
        self.assertIn("REPLY FORMAT", out["prompt"])
        self.assertIn("for (j = 0; j < N; j++)", out["prompt"].replace("700", "N"))

    # ---------------------------------------------------------------- local language model
    def _with_env(self, **env):
        old = {k: os.environ.get(k) for k in env}
        os.environ.update(env)
        self.addCleanup(lambda: [os.environ.pop(k, None) if v is None else os.environ.__setitem__(k, v) for k, v in old.items()])

    def test_llm_status_endpoint(self):
        self._with_env(OLLAMA_HOST="127.0.0.1:9", HTC_LLM_NO_AUTOSTART="1")
        status, payload = self.call("GET", "/api/llm/status")
        out = json.loads(payload)
        self.assertEqual(status, 200)
        self.assertIn("ready", out)
        self.assertFalse(out["ready"])
        self.assertIn("message", out)

    def test_ai_unavailable_is_reported_not_faked(self):
        self._with_env(OLLAMA_HOST="127.0.0.1:9", HTC_LLM_NO_AUTOSTART="1")
        status, out = self.analyze(source=COLUMN_WALK, topology=TOPO, optimize=True, bench=False)   # ai defaults to true
        self.assertEqual(status, 200)
        self.assertFalse(out["ok"])
        self.assertTrue(out.get("ai_unavailable"))
        self.assertIn("no AI analysis was performed", out["error"])
        self.assertNotIn("report", out)

    def test_offline_mode_needs_no_model(self):
        self._with_env(OLLAMA_HOST="127.0.0.1:9", HTC_LLM_NO_AUTOSTART="1")
        status, out = self.analyze(source=COLUMN_WALK, topology=TOPO, optimize=True, ai=False, bench=False)
        self.assertTrue(out["ok"], out)
        self.assertEqual(out["report"]["optimization"]["ai"]["mode"], "offline-rules")

    def test_model_name_is_validated(self):
        status, out = self.analyze(source=COLUMN_WALK, optimize=True, llm_model="x; rm -rf /")
        self.assertEqual(status, 400)
        status, out = self.analyze(source=COLUMN_WALK, optimize=True, ai="yes")
        self.assertEqual(status, 400)

    @unittest.skipUnless(shutil.which("gcc") and shutil.which("python3"), "needs gcc")
    def test_dashboard_to_compiler_to_model_MOCKED(self):
        """MOCKED model (tests/mock_ollama.py replays a fixture); checks the plumbing, not inference."""
        mock = subprocess.Popen([sys.executable, os.path.join(PROJECT, "tests", "mock_ollama.py"),
                                 "--reply", os.path.join(PROJECT, "tests", "llm", "valid_transpose.json"),
                                 "--models", "mock-coder:3b"], stdout=subprocess.PIPE, text=True)
        self.addCleanup(mock.kill)
        port = mock.stdout.readline().split()[1]
        self._with_env(OLLAMA_HOST="127.0.0.1:" + port, HTC_LLM_NO_AUTOSTART="1")
        src = COLUMN_WALK.replace("700", "6")            # tiny arrays: nothing here is about speed
        status, out = self.analyze(source=src, topology=TOPO, optimize=True, bench=False)
        self.assertTrue(out["ok"], out)
        ai = out["report"]["optimization"]["ai"]
        self.assertEqual(ai["mode"], "local-model")
        self.assertEqual(ai["model"], "mock-coder:3b")
        self.assertEqual(ai["proposals"]["total"], 2)
        self.assertEqual(ai["proposals"]["unsupported"], 1)
        status, payload = self.call("GET", "/api/llm/status")
        self.assertTrue(json.loads(payload)["ready"])

    def test_unknown_routes(self):
        self.assertEqual(self.call("GET", "/nope")[0], 404)
        self.assertEqual(self.call("POST", "/api/other", body={})[0], 404)


if __name__ == "__main__":
    unittest.main(verbosity=2)
