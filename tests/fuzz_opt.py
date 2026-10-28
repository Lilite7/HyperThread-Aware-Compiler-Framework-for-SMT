#!/usr/bin/env python3
"""Soundness fuzzer for the optimizer's dependence analysis.

For random loop nests with random affine subscripts it
  1. asks htcompiler (--opt-plan, nothing is built or run) whether a loop interchange or a
     loop fusion is legal, and
  2. executes the program in the original and in the transformed loop order with a small
     reference interpreter on several random data sets.

SOUNDNESS: whenever htcompiler says "exact" the two orders must give identical results.
PRECISION: how often htcompiler refuses something the brute force shows to be legal
           (a missed opportunity, not a bug).

    python3 tests/fuzz_opt.py [programs] [seed]       exit status 1 on any soundness violation
"""
import json
import os
import random
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
HTC = os.path.join(os.path.dirname(HERE), "htcompiler")

SIZE1 = 64          # 1-D array length
SIZE2 = 8           # 2-D array side
ARRAYS1 = ["A", "B"]
ARRAYS2 = ["P", "Q"]


# ---------------------------------------------------------------- program model
class Ref:
    """array[sub] or array[sub0][sub1]; each sub = (const, {var: coef})"""
    def __init__(self, name, subs):
        self.name, self.subs = name, subs

    def text(self):
        parts = []
        for const, coefs in self.subs:
            terms = []
            for v, c in sorted(coefs.items()):
                if c == 0:
                    continue
                terms.append(("-" if c < 0 else "+") + ("" if abs(c) == 1 else str(abs(c)) + "*") + v)
            expr = "".join(terms)
            if const or not expr:
                expr += ("+" if const >= 0 and expr else "") + str(const)
            if expr.startswith("+"):
                expr = expr[1:]
            parts.append("[" + expr + "]")
        return self.name + "".join(parts)

    def index(self, env):
        out = []
        for const, coefs in self.subs:
            out.append(const + sum(c * env[v] for v, c in coefs.items()))
        return tuple(out)


class Stmt:
    """target (op) src1 [+ src2] [+ k]   op in '=', '+='; or scalar  s += src1"""
    def __init__(self, target, op, srcs, k):
        self.target, self.op, self.srcs, self.k = target, op, srcs, k

    def text(self):
        rhs = " + ".join(s.text() for s in self.srcs)
        if self.k:
            rhs += " + %d" % self.k
        tgt = "s" if self.target is None else self.target.text()
        return "%s %s %s;" % (tgt, self.op, rhs)

    def run(self, env, arrays, scal):
        val = sum(arrays[s.name][s.index(env)] for s in self.srcs) + self.k
        if self.target is None:
            scal[0] += val
            return
        idx = self.target.index(env)
        if self.op == "=":
            arrays[self.target.name][idx] = val
        else:
            arrays[self.target.name][idx] += val


def rand_sub(rng, vars_, size, ndim):
    """affine subscript guaranteed to stay inside [0, size) for loop vars in [0, bound)"""
    coefs = {v: rng.choice([0, 0, 1, 1, 1, 2, -1]) for v in vars_}
    if all(c == 0 for c in coefs.values()):
        coefs[rng.choice(vars_)] = 1
    lo = sum(min(0, c) * (BOUND - 1) for c in coefs.values())
    hi = sum(max(0, c) * (BOUND - 1) for c in coefs.values())
    room = size - 1 - (hi - lo)
    if room < 0:
        return None
    const = -lo + rng.randint(0, min(room, 3))
    return (const, coefs)


def rand_ref(rng, vars_):
    if rng.random() < 0.55:
        name = rng.choice(ARRAYS1)
        sub = rand_sub(rng, vars_, SIZE1, 1)
        return Ref(name, [sub]) if sub else None
    name = rng.choice(ARRAYS2)
    s0, s1 = rand_sub(rng, vars_, SIZE2, 2), rand_sub(rng, vars_, SIZE2, 2)
    return Ref(name, [s0, s1]) if s0 and s1 else None


def stencil_stmt(rng, vars_):
    """X[v0+a][v1+b] (=|+=) X[v1'+c][v0'+d] + k : creates loop-carried dependences with small distances"""
    name = rng.choice(ARRAYS2)
    v0 = rng.choice(vars_)
    v1 = rng.choice([v for v in vars_ if v != v0] or vars_)
    a, b = rng.choice([1, 2]), rng.choice([1, 2])
    tgt = Ref(name, [(a, {v0: 1}), (b, {v1: 1})])
    da, db = rng.choice([-1, 0, 1]), rng.choice([-1, 0, 1])
    if rng.random() < 0.2:
        src = Ref(name, [(b + db, {v1: 1}), (a + da, {v0: 1})])      # transposed read
    else:
        src = Ref(name, [(a + da, {v0: 1}), (b + db, {v1: 1})])
    if rng.random() < 0.3:
        src2 = Ref(rng.choice(ARRAYS1), [(rng.randint(0, 3), {v0: 1})])
        return Stmt(tgt, rng.choice(["=", "="]), [src, src2], rng.choice([0, 1]))
    return Stmt(tgt, rng.choice(["=", "=", "+="]), [src], rng.choice([0, 1]))


def rand_stmt(rng, vars_):
    if rng.random() < 0.45:
        return stencil_stmt(rng, vars_)
    for _ in range(50):
        srcs = [rand_ref(rng, vars_) for _ in range(rng.choice([1, 1, 2]))]
        if any(s is None for s in srcs):
            continue
        k = rng.choice([0, 0, 1, 2])
        if rng.random() < 0.15:
            return Stmt(None, "+=", srcs, k)                     # scalar sum reduction
        tgt = rand_ref(rng, vars_)
        if tgt is None:
            continue
        return Stmt(tgt, rng.choice(["=", "=", "+="]), srcs, k)
    return None


BOUND = 5


def make_arrays(rng):
    arrays = {}
    for n in ARRAYS1:
        arrays[n] = {(i,): rng.randint(-5, 5) for i in range(SIZE1)}
    for n in ARRAYS2:
        arrays[n] = {(i, j): rng.randint(-5, 5) for i in range(SIZE2) for j in range(SIZE2)}
    return arrays


# ---------------------------------------------------------------- C text
HEADER = "".join("int %s[%d];\n" % (n, SIZE1) for n in ARRAYS1) + \
         "".join("int %s[%d][%d];\n" % (n, SIZE2, SIZE2) for n in ARRAYS2)


def nest_text(vars_, body, indent=1):
    lines = []
    for d, v in enumerate(vars_):
        lines.append("    " * (indent + d) + "for (%s = 0; %s < %d; %s++) {" % (v, v, BOUND, v))
    for st in body:
        lines.append("    " * (indent + len(vars_)) + st.text())
    for d in reversed(range(len(vars_))):
        lines.append("    " * (indent + d) + "}")
    return lines


def program(nests):
    """nests: list of (vars, body).  returns (text, first-loop line of each nest)"""
    lines = HEADER.rstrip("\n").split("\n")
    lines += ["", "int main(void)", "{", "    int i;", "    int j;", "    int m;", "    int s = 0;"]
    firsts = []
    for vars_, body in nests:
        firsts.append(len(lines) + 1)
        lines += nest_text(vars_, body)
    lines += ["    return s;", "}"]
    return "\n".join(lines) + "\n", firsts


# ---------------------------------------------------------------- brute force
def run_nest(vars_, body, order, arrays, scal):
    """order: permutation of range(len(vars_)) = loop order outermost first"""
    def rec(level, env):
        if level == len(order):
            for st in body:
                st.run(env, arrays, scal)
            return
        v = vars_[order[level]]
        for x in range(BOUND):
            env[v] = x
            rec(level + 1, env)
    rec(0, {})


def snapshot(arrays, scal):
    return (scal[0], tuple(sorted((n, tuple(sorted(a.items()))) for n, a in arrays.items())))


def equal_under(rng, trials, run_a, run_b):
    for _ in range(trials):
        seed = rng.random()
        r1, r2 = random.Random(seed), random.Random(seed)
        a1, a2 = make_arrays(r1), make_arrays(r2)
        s1, s2 = [0], [0]
        run_a(a1, s1)
        run_b(a2, s2)
        if snapshot(a1, s1) != snapshot(a2, s2):
            return False
    return True


# ---------------------------------------------------------------- driver
def plan(text, workdir):
    src = os.path.join(workdir, "f.c")
    with open(src, "w") as f:
        f.write(text)
    r = subprocess.run([HTC, src, "--opt-plan", "--quiet", "-o", os.path.join(workdir, "o.c"),
                        "--json", os.path.join(workdir, "o.json"), "--topology", "0,1"],
                       capture_output=True, text=True)
    if r.returncode != 0:
        return None, r.stderr
    with open(os.path.join(workdir, "o.json")) as f:
        return json.load(f)["optimization"]["candidates"], ""


def main():
    count = int(sys.argv[1]) if len(sys.argv) > 1 else 300
    seed = int(sys.argv[2]) if len(sys.argv) > 2 else 1
    rng = random.Random(seed)
    stats = dict(programs=0, ic_legal=0, ic_refused=0, ic_missed=0, fu_legal=0, fu_refused=0, fu_missed=0,
                 bad=0, compile_errors=0)
    violations = []
    with tempfile.TemporaryDirectory() as wd:
        for n in range(count):
            kind = rng.choice(["interchange", "interchange", "fusion"])
            if kind == "interchange":
                depth = rng.choice([2, 2, 3])
                vars_ = ["i", "j", "m"][:depth]
                body = [s for s in (rand_stmt(rng, vars_) for _ in range(rng.randint(1, 3))) if s]
                if not body:
                    continue
                text, firsts = program([(vars_, body)])
                cands, err = plan(text, wd)
                if cands is None:
                    stats["compile_errors"] += 1
                    violations.append(("compile error", text, err))
                    continue
                stats["programs"] += 1
                # loops sit on consecutive lines: candidate at line firsts[0]+k swaps loops k and k+1
                for k in range(depth - 1):
                    line = firsts[0] + k
                    order = list(range(depth))
                    order[k], order[k + 1] = order[k + 1], order[k]
                    found = [c for c in cands if c["transformation"] == "interchange" and c["line"] == line]
                    truth = equal_under(
                        rng, 6,
                        lambda a, s: run_nest(vars_, body, list(range(depth)), a, s),
                        lambda a, s, o=order: run_nest(vars_, body, o, a, s))
                    if not found:
                        continue
                    verdict = found[0]["safety"]
                    if verdict == "exact":
                        stats["ic_legal"] += 1
                        if not truth:
                            stats["bad"] += 1
                            violations.append(("UNSOUND interchange at loop %d" % k, text, found[0]))
                    else:
                        stats["ic_refused"] += 1
                        if truth:
                            stats["ic_missed"] += 1
            else:
                body1 = [s for s in (rand_stmt(rng, ["i"]) for _ in range(rng.randint(1, 2))) if s]
                body2 = [s for s in (rand_stmt(rng, ["i"]) for _ in range(rng.randint(1, 2))) if s]
                if not body1 or not body2:
                    continue
                text, firsts = program([(["i"], body1), (["i"], body2)])
                cands, err = plan(text, wd)
                if cands is None:
                    stats["compile_errors"] += 1
                    violations.append(("compile error", text, err))
                    continue
                stats["programs"] += 1
                found = [c for c in cands if c["transformation"] == "fusion" and c["line"] == firsts[0]]

                def sequential(a, s):
                    run_nest(["i"], body1, [0], a, s)
                    run_nest(["i"], body2, [0], a, s)

                def fused(a, s):
                    for x in range(BOUND):
                        env = {"i": x}
                        for st in body1:
                            st.run(env, a, s)
                        for st in body2:
                            st.run(env, a, s)

                truth = equal_under(rng, 6, sequential, fused)
                if not found:
                    continue
                if found[0]["safety"] == "exact":
                    stats["fu_legal"] += 1
                    if not truth:
                        stats["bad"] += 1
                        violations.append(("UNSOUND fusion", text, found[0]))
                else:
                    stats["fu_refused"] += 1
                    if truth:
                        stats["fu_missed"] += 1

    print("programs generated        : %d" % stats["programs"])
    print("interchange  judged legal : %d   refused: %d   (refused but actually legal: %d)"
          % (stats["ic_legal"], stats["ic_refused"], stats["ic_missed"]))
    print("fusion       judged legal : %d   refused: %d   (refused but actually legal: %d)"
          % (stats["fu_legal"], stats["fu_refused"], stats["fu_missed"]))
    print("SOUNDNESS VIOLATIONS      : %d" % stats["bad"])
    print("compile errors            : %d" % stats["compile_errors"])
    for what, text, extra in violations[:3]:
        print("\n--- %s\n%s\n%s" % (what, text, extra))
    return 1 if violations else 0


if __name__ == "__main__":
    sys.exit(main())
