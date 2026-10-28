# HT-Compass – Design Notes

## 1. Mapping to the report (Chapter 3.2)

| Report module | Where it lives |
|---|---|
| 3.2.1 Input and Preprocessing | `lexer.l` – records `#include`, expands numeric `#define`s, strips comments |
| 3.2.2 Lexical Analyzer | `lexer.l` |
| 3.2.3 Parser and AST Generation | `parser.y`, `ast.c` |
| 3.2.4 Parallelism Detection | `analysis.c` – `discover_threads()` finds `pthread_create` sites and expands creation loops; `sema.c` validates them |
| 3.2.5 Hyperthread Resource Analysis | `profiler.c` (per-thread profile) + `contention.c` `score_pair()` (pair interference) |
| 3.2.6 Hyperthread Optimization | `contention.c` `make_mapping()` – minimum-weight matching; the *advice* strings in the JSON say whether SMT co-location pays off |
| 3.2.7 JSON Report Generation | `report.c` |
| 3.2.8 C Code Generation | `codegen.c` |

## 2. Front end

### Lexer (Flex)
Tokens: keywords, identifiers, integer / float / string literals, operators. Special handling:

- `#include <…>` lines are stored and re-emitted; `#define NAME number` is stored **and expanded at
  lex time**, so `a[N]` and `i < N` are plain constants for later phases. Any other directive is rejected.
- Constructs outside the subset (`switch`, `struct`, `goto`, `do`, `char`, `long`, `static`, `->`,
  `<<`, `?:` …) abort with `Unsupported construct at line N: …`.

### Parser (Bison)
One non-terminal per precedence level (`logical_or` … `postfix`), so there are no precedence
declarations to reason about. Only the classic dangling-`else` precedence is used. The grammar has
**zero shift/reduce and zero reduce/reduce conflicts** (`bison -Wconflicts-sr -Wconflicts-rr`).
Syntactically recognisable but unsupported forms (`*p`, `(int)x`) are rejected with a clear message
instead of a generic syntax error.

### AST
A single generic `Node` struct (kind, text, children `a b c d`, sibling `next`) keeps the code
small; `ast.h` documents which field means what for every node kind. `--dump-ast` prints the tree.

### Semantic analysis (`sema.c`)
Scoped symbol table; every expression gets a type (`etype`), every identifier its `Symbol`. Checks:
undeclared / redeclared names, array subscript count and type, `%` on floating point, argument counts,
`main` exists, and the pthread rules (`pthread_create(&t, NULL, worker, arg)` with
`void *worker(void *)`, `pthread_join(t, NULL)`, mutex calls take `&m`).

## 3. Analyses

### 3.1 Thread discovery and concurrency (`analysis.c`)
- Every `pthread_create` becomes a **site**. A site inside one canonical `for` loop with constant
  bounds expands into *trip count* **instances**; a loop is canonical if it has the form
  `for (v = c0; v <op> c1; v++ | v += c)`.
- Source order of `pthread_create` / `pthread_join` calls defines an **event timeline**. A thread lives
  from its create event to the join event of the same `pthread_t` variable (or forever if never joined).
  Two threads *may overlap* iff their intervals intersect. Instances created and joined in the same loop
  body are serial and never overlap each other.
- Threads that cannot overlap get contention 0 – this is why create/join ordering matters to the result.

### 3.2 Static resource profile (`profiler.c`)
The worker's AST (plus callees, recursion-guarded) is walked with a weight equal to the product of
enclosing trip counts. Counted: integer ops, FP add/mul, *heavy* ops (`/ %`, weight 10 for math calls),
array loads/stores, branches, I/O and lock operations. Scalars are assumed to be in registers.

**Access classification.** For each array access, relative to the innermost loop variable `v`:

| pattern | rule | lines per access |
|---|---|---|
| sequential | index is affine in `v` with slope 1 | `elem/64` |
| strided | constant slope s > 1 (2-D: `slope_row·cols + slope_col`) | `min(1, s·elem/64)` |
| irregular | index contains an array read (`a[b[i]]`) or a variable the loop reassigns non-affinely (`x = next[x]`) | 1 |
| invariant | index does not depend on `v` | 0 |

**Memory traffic.** `lines` = Σ accesses × lines-per-access. An array that fits in half of L2 only
costs its compulsory misses (`bytes/64`); a larger array pays every miss. The L1 **footprint** of an array is
`clamp(lines·64, 64, bytes)`.

Normalised demands (each 0‥1, dividing by `total_ops`): `n_int`, `n_fp`, `n_heavy`, `n_ldst`, and
`n_bw = min(1, (DRAM bytes / op) / BW_REF)` with `BW_REF = 4` bytes per operation.

Class label: *latency-bound* (irregular & bw > 0.2), *memory-bound* (bw ≥ 0.3), *compute-bound
(div/sqrt/math)* (heavy ≥ 10 %), *compute-bound (floating point)* (fp ≥ 15 %), *cache-resident*
(load/store ≥ 35 %), else *compute-bound (integer)*.

### 3.3 Pairwise contention (`contention.c`)

```
score(A,B) = 100 · overlap · lockf · ( 0.30·exec + 0.15·ldst + 0.25·cache + 0.30·bw )

exec  = min(1, 0.4·min(intA,intB) + 0.8·min(fpA,fpB) + 2.0·min(heavyA,heavyB))
ldst  = min(ldstA, ldstB)
cache = min(cdA, cdB) · pressure          cd = clamp(footprint / (L1/4)),
                                          pressure = clamp((log2(union/L1) + 1) / 3)
bw    = min(bwA, bwB)
overlap = 0 if the threads never run concurrently, else (1 + min(wA,wB)/max(wA,wB)) / 2
lockf   = 1 − 0.15·min(2, number of shared mutexes)
```

Every component uses **min(demandA, demandB)** – contention needs two parties; a compute thread
cannot hurt a streaming thread's bandwidth. `union` is the combined footprint with arrays used by
both threads counted once (constructive sharing in the shared L1D). `overlap` is the mean fraction of the
two lifetimes that are overlapped, assuming equal throughput.

Severity: **high** ≥ 35, **medium** ≥ 15, otherwise low.

#### Worked example (`tests/mixed.c`, T0 = `stream_a`, T1 = `stream_b`)
Per iteration of `a[i] = b[i]*3.0 + c[i]`: 2 loop-overhead int ops, 2 FP ops, 2 loads, 1 store,
1 branch → 8 ops, so `int = fp = 0.25`, `ldst = 0.375`. Traffic: 3 accesses × 8/64 lines = 24 B per
iteration = 3 B/op → `bw = 0.75`. Footprints are ≈ 46 MiB each, far above L1 → `cache = 1`.

```
exec = 0.4·0.25 + 0.8·0.25 = 0.30
raw  = 0.30·0.30 + 0.15·0.375 + 0.25·1.0 + 0.30·0.75 = 0.62125   →  score 62.1  (high)
```
Pairing T0 with `fp_compute_1` (no memory traffic) leaves only the FP-unit term:
`0.30·0.30 · 0.969 = 8.7` (low). The optimizer therefore pairs each streaming thread with a compute thread:
predicted total **76.5 → 17.4**.

### 3.4 Mapping optimizer
Choose which threads share a core so that the **sum of pair scores is minimal**:

- `pack` policy: `p = ⌊N/2⌋` pairs. `spread` policy: `p = max(0, N − K)` pairs, where K is the number of
  physical cores (sharing is only as much as the core count forces).
- The remaining `N − 2p` threads run alone. Add one dummy node per lonely thread (real–dummy cost 0,
  dummy–dummy forbidden) and solve a **minimum-weight perfect matching**.
- Exact for ≤ 20 nodes: DP over bitmasks, `O(2ⁿ·n)`. Larger inputs use a greedy pass that takes the `p`
  cheapest disjoint pairs.
- Baselines for evaluation use the same `p`: `naive` pairs threads in creation order, `worst` solves the
  maximisation problem.
- Groups are placed on physical cores `0, 1, …` (wrapping with a warning if there are more groups than
  cores); the first thread of a group takes the core's first sibling CPU, the second takes the second.

## 4. Code generation (`codegen.c`)
The AST is unparsed back to C (operator-precedence-aware parenthesisation, braces on every body).
Added to the output: the header comment with the mapping, `_GNU_SOURCE`, `__ht_cpu_map[]`, `__ht_create()`,
prototypes of all functions. A creation site becomes `__ht_create(&t[i], worker, arg, <thread id>)`;
inside a creation loop the thread id is `first + (i − start) / step`.

## 5. Constants (`src/ht.h`)
| name | value | meaning |
|---|---|---|
| `DEFAULT_TRIPS` | 64 | assumed trip count of loops with unknown bounds |
| `BW_REF` | 4.0 | bytes of DRAM traffic per operation that counts as "bandwidth saturated" |
| `W_EXEC / W_LDST / W_CACHE / W_BW` | .30 / .15 / .25 / .30 | component weights (sum to 1) |
| `SEV_HIGH / SEV_MED` | 35 / 15 | severity thresholds |

Changing a constant and re-running `make test` shows immediately which conclusions are sensitive to it –
good material for the discussion section.

## 6. Likely viva questions

**Why can a compiler say anything about SMT contention?** Contention depends on what each thread
does (compute mix, memory pattern, footprint) and which threads run together. The first is largely
visible in the code, the second in create/join order. The compiler cannot see runtime input or other
processes, so the result is a *prediction*, not a guarantee.

**Why min(demandA, demandB)?** Interference needs two consumers of the same resource. It is symmetric,
bounded by the lighter user, and zero when either thread does not use the resource.

**Why is the optimum a matching problem?** Each core hosts two threads; the cost of a placement is the sum of
pair scores; every thread is in exactly one pair → minimum-weight perfect matching on a complete graph.

**Why dummy nodes?** When some threads run alone the number of pairs is fixed; connecting each lonely thread to a
zero-cost dummy turns it back into a perfect matching.

**Why not always spread threads over cores?** When cores are scarce (shared host, containers, more threads than
cores) sharing is unavoidable, and `pack` models the case where SMT is used on purpose. `--policy spread` computes
the minimum sharing; with enough cores it reports zero contention.

**Why Flex/Bison and not hand-written parsing?** The subset is a real grammar with precedence and dangling-else;
Bison checks it for conflicts, and error positions come for free.

**How is `x = next[x]` detected as irregular?** A variable used in an index that the loop body reassigns by something
other than `x++`, `x += c`, or `x = x ± c` is data-dependent, so the access gets the full-miss cost.

**What does the generated program change?** Only where threads run: each `pthread_create` becomes a
`__ht_create` that sets a CPU-affinity attribute before the thread starts.

**What are the weaknesses?** Static heuristic weights, constant trip counts, scalars-in-registers assumption, pinning
as the only transformation, Linux-only affinity API. See *Limitations* in the README.

**How would you validate it?** `bench/run_bench.sh` on an SMT machine: compare baseline / naive / optimized / worst wall
times and `perf stat` counters; check that predicted ranking matches measured ranking.

## 7. Possible extensions
Calibrate the weights by regression against measured co-run slowdowns · follow calls that create threads ·
support thread arguments · simple loop transformations (tiling, interchange) driven by the access classification ·
emit OpenMP `proc_bind` / `OMP_PLACES` instead of pthread affinity · read hardware counters to refine the model.

## 8. Optimization pipeline

```
program ──► analyse (threads, cost model)
              │
              ▼
        find opportunities ─── every transformation in the registry (interchange, fusion, promotion)
              │                 each carries a legality verdict:  exact | fp-reassoc | refused (+ reason)
              ▼
   for each legal opportunity (advice first, then those expected to help):
        apply to a COPY ──► re-run the cost model ──► differential test ──► benchmark
              │
              ▼
        best candidate that passed everything ──► re-measure it ──► apply ──► next round
              │
              ▼
        final end-to-end timing: if the changes did not add up to a gain, revert them all
```

Source files: `transform.c` (registry), `depend.c` (legality), `optimize.c` (driver), `verify.c` (tests and
timing), `runner.c` (running gcc and programs under test), `astutil.c` (cloning and canonical keys).

### 8.1 Legality (`depend.c`)

Every array reference in the region gets one subscript per dimension, classified as *affine* (a constant plus
constant multiples of the nest's loop variables), *symbolic* (no nest variable, e.g. an enclosing loop variable) or
*other* (indirect `a[b[i]]`, `a[i*n+j]`). For two references to the same array where one is a write, the subscript
equations give a dependence distance for each loop variable; variables that appear in no equation are free.

- **Interchange / permutation:** legal iff for every sign pattern of the free components, every dependence vector
  is still lexicographically positive after permuting the loops. A pair the equations cannot solve is *unknown*, and
  unknown means refuse.
- **Fusion:** the two loops must have the identical iteration space and loop variable; every element shared between them
  must be reached by loop 1 no later than by loop 2.
- **Scalars:** only private temporaries, loop variables and pure `+=`/`-=` reductions may change order. A floating-point
  reduction reorders rounding, so it is classified *fp-reassoc* and refused unless `--fp-reassoc` is given.
- **Region contents:** calls (other than math), `return`, `&x` and I/O make the region unanalysable, hence refused.
- **Scope:** the finder additionally requires constant loop bounds and that no loop variable is modified in the nest
  (the scan alone would not notice that).
- **Scalar promotion** is decided by simple rules rather than distance vectors: the loop must be innermost and
  straight-line, the element's subscripts must not change in the loop, no other element of that array may be touched
  (it could alias), calls are forbidden, and the loop must run at least once. For global arrays used by threads the
  array must not be touched by another thread function or by several instances of the same worker.

### 8.2 Cost model changes

Two refinements made the model usable for comparing programs:

- an element touched again in the same iteration is a cache hit (this is what makes fusion visible);
- an access that does not change in the innermost loop is charged at the outer loop it *does* vary with. Without this,
  promoting `c[i][j]` out of the `k` loop appeared to *increase* memory traffic.

The model's cost is `operations + 8 × estimated memory lines`, plus the predicted contention of the best thread mapping.
It prioritises; when timing is available timing decides.

### 8.3 Differential test

For both programs the code generator emits an *instrumented* build: `main` is wrapped so that, when `HT_SEED` is set,
every global is filled with deterministic pseudo-random data, and at exit a fingerprint of every global is written
(an exact hash, plus sum, sum of squares and maximum for floating point). Runs: the program's own initial data, then
`--opt-seeds` random data sets. Exact-class changes must match bit for bit and print the same sorted output;
fp-reassoc changes are compared with a relative tolerance of 1e-7. Test builds use `-ffp-contract=off`.
If the *original* does not run cleanly, nothing can be verified and every candidate is rejected (fail closed).

### 8.4 Benchmark methodology

Interleaved runs with alternating order, best-of-N (N grows for short programs), and a median check that must show half
the required gain. The winning candidate is measured again, twice as long, before it may change the program: choosing
the best of several noisy numbers is biased upward (the winner's curse), and in an early version this selected a fusion
that was slower end to end. A final end-to-end timing reverts everything if the combined result is not faster.
Programs under about 20 ms cannot be timed; they are decided by the cost model with a stricter 3% threshold.

### 8.5 What the testing found

| bug | how it showed up |
|---|---|
| scanning one loop also scanned all following statements | every loop appeared to contain the `printf` after it, so nothing was ever legal |
| invariant accesses charged nothing | promotion looked like it increased memory traffic |
| picking the best of several noisy timings | a fusion measured 1.15× while the program got slower end to end |
| a weak fuzzer | it missed a deliberately broken interchange check until the generator produced stencil-style dependences |

## 9. More viva questions

**Why both prove and test?** The proof (dependence analysis) covers only a restricted class but covers *all* inputs;
the test covers any program the rewriting produces but only the inputs tried. They fail differently: a bug in the proof
is likely to be caught by the test, and the test cannot be fooled by a particular input the proof ruled out.

**Where does the AI come in, and why can it not break anything?** It can suggest where to look (the compiler writes a
prompt, you give it to any chat AI, and the reply is read back as `transform function line`). The suggestion only moves a
candidate to the front of the queue; legality, testing and timing are the compiler's.

**What does "exact" mean?** The transformed program performs the same operations on the same values in an order
that cannot change any result, so memory and output are identical. Floating-point reordering is the only case the
analysis separates out, because it is legal but changes rounding.

**Why did the optimizer reject scalar promotion on matrix multiply?** It was proven safe and tested, but measured no
gain: gcc at `-O2` already keeps `c[i][j]` in a register. A verdict like this is the benchmark doing its job.

**What if the program has a data race?** The transformations assume the program is correct apart from what the
compiler can check, as a normal optimizing compiler does. Where the compiler can see a possible race on an array it
refuses (promotion on a shared global).

**How could the analysis still be wrong?** In the rewriting or the model of the language: for example a subscript
form it mishandles, or an aliasing case it does not consider. That is why there are three independent checks and why
the fuzzer is tested by deliberately breaking the analysis.

**Why are there so many "refused" verdicts?** Refusal is the safe failure: about a fifth of nests the fuzzer shows to
be legal are refused because the analysis could not prove them. Improving that is a precision problem, not a safety one.

## 10. Local language model integration

### 10.1 Division of labour
The model *discovers, explains and prioritises*; the compiler *applies and verifies*. The reply is parsed as data
(`src/json.c`, bounded: depth 24, 20,000 values, 256 KB per string, 2 MB total) and reduced to a list of proposals. A proposal is
a (transformation, region, parameter) triple plus text for the report. Nothing in it is executed, compiled or pasted into the
program, so a reply cannot change what the compiler does except by choosing among transformations the compiler itself implements.

### 10.2 Stable regions
A line number is a poor name: it changes with every edit. A region id is `function/L<n>`, the n-th loop of the function in
source order (`loop_ordinal` in `transform.c`); the same function numbers loops for the prompt and for every finder, so a
proposal and a candidate meet at the same string. The prompt also gives each loop its iteration count, nesting and the stride
of each array reference with respect to that loop (from the dependence analysis' affine machinery), which is what lets a
model say "the second nest walks `b` with stride 3000" instead of guessing.

### 10.3 Finder, precondition checker, applier
A transformation is registered with one `scan(program, ctx, check, ...)` function and an `apply`. With `check = 0` the scan is
the **finder** (where does the pattern occur); with `check = 1` it is also the **precondition checker** (dependence analysis,
alias and thread rules); `apply` is the **applier**. `transform_find_sites`, `transform_check`, `transform_find` and
`transform_apply` expose the three roles. A proposal is attached to the candidate whose transformation and region match; if
none matches, the proposal is reported as having no applicable site, with the reason that the pattern is outside the supported
class.

### 10.4 New transformations
- **Unrolling** replays the iterations in the original order, so no dependence can change; it needs a constant trip count and an
  unmodified loop variable, and emits an epilogue loop for the remainder. The cost model had to learn that `a[i], a[i+1], ...`
  share cache lines, otherwise unrolling looked like a four-fold increase in memory traffic.
- **Loop-invariant code motion** hoists maximal pure subexpressions: constants, loop-invariant scalars, `+ - *`, floating-point
  division, division or modulo by a positive constant, and math functions. Right operands of `&&`/`||` and `if` branches are
  never hoisted (they may not execute, and hoisting could introduce an overflow that did not happen). Globals are treated as
  variant if the loop calls anything other than math, or if the program has threads and any function writes them.

### 10.5 Failure policy
| situation | result |
|---|---|
| service not running | started automatically when `ollama` is on PATH, otherwise exit 3 with install instructions |
| no model installed / requested model missing | exit 3 with the exact `ollama pull` command |
| timeout, oversized reply, HTTP error, non-JSON server | exit 3, nothing written |
| reply is malformed, cut off, wrong shape | the problem is reported; the compiler's own findings are still evaluated and labelled `compiler` |
| proposal names an unknown region / no region / is a duplicate | reported as invalid / duplicate, not evaluated |
| proposal names something not implemented | reported as unsupported, never as applied |
| anything the model proposes that is unsafe or slower | rejected by the proof, the test or the benchmark, with the reason |

### 10.6 What the tests prove, and what they do not
Mocked tests prove the plumbing and the validation: they use replies the author wrote. They say nothing about what a model
will actually propose. The only real inference run was a 270M base model that could not follow the instructions, which shows
the pipeline survives useless output but not that useful output gets through. A proper instruction-tuned model has not been
tested, and no claim is made about its proposal quality.

### 10.7 Viva questions
**Why not let the model write the optimized code?** It cannot be trusted to preserve semantics, and checking arbitrary code is
far harder than checking a rewrite the compiler itself performs. Here the model only chooses where to look.
**What stops a bad suggestion?** Three independent checks: a dependence proof, a differential test on original and random data,
and a timing comparison. **Why does `--optimize` fail instead of falling back?** A silent fallback would make a report claim AI
involvement that did not happen. **Why local?** No key, no account, no data leaves the machine; the cost is your own CPU and RAM.
