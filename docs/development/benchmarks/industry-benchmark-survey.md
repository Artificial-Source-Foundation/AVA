# Industry Coding Benchmark Survey

Research conducted March 2026 for AVA's benchmark system design.

## Major Benchmarks

### 1. SWE-bench / SWE-bench Verified
- **What**: Real GitHub issue resolution. Given codebase + issue, generate a patch.
- **Scale**: 2,294 tasks (original), 500 verified subset from 12 Python repos.
- **Eval**: Unit tests (FAIL_TO_PASS must pass, PASS_TO_PASS must not regress). Docker containers.
- **SOTA**: ~76% pass@1 (Verdent), ~81% pass@3.
- **Strengths**: Gold standard for real-world SE; human-verified subset.
- **Weaknesses**: Python-only, bug-fix dominated, scaffold matters as much as model.

### 2. HumanEval / HumanEval+ / MBPP / MBPP+
- **What**: Single-function code gen from docstrings (HumanEval) or NL descriptions (MBPP).
- **Scale**: 164 (HE), 1,000 (MBPP). EvalPlus adds 80x/35x more test cases.
- **SOTA**: 96.2% on HumanEval (o1-mini). Effectively saturated.
- **Strengths**: Simple, widely reported. EvalPlus variants improve reliability.
- **Weaknesses**: Saturated, Python-only, single-function, high contamination risk.

### 3. LiveCodeBench
- **What**: Competitive programming with contamination-free temporal filtering.
- **Scale**: 1,055 problems (v6). LeetCode, AtCoder, CodeForces.
- **Strengths**: Contamination-free by design, continuously updated.
- **Weaknesses**: Competitive programming bias, single-file solutions only.

### 4. Aider Polyglot
- **What**: Code gen and editing across 6 languages (C++, Go, Java, JS, Python, Rust).
- **Scale**: 225 Exercism problems. Two-attempt model (generate + retry on failure).
- **SOTA**: ~93% (Refact.ai Agent + Claude 3.7 Sonnet with thinking).
- **Strengths**: Multilingual, tests error correction, tests file editing.
- **Weaknesses**: Single-file, 225 problems, contamination risk.

### 5. BigCodeBench
- **What**: Code gen requiring diverse function calls from 139 popular libraries.
- **Scale**: 1,140 tasks. Two variants: Complete (docstrings) and Instruct (NL).
- **Strengths**: Tests real library/API usage, significant unsolved headroom.
- **Weaknesses**: Python-only, single-function scope.

### 6. BFCL v4 (Berkeley Function Calling Leaderboard)
- **What**: Tool/function calling evaluation.
- **Scale**: 2,000+ scenarios. V4 adds agentic evaluation: multi-hop, error recovery, memory.
- **Strengths**: De facto standard for tool-use evaluation.
- **Weaknesses**: Not integrated with real codebase tasks.

### 7. SWE-EVO
- **What**: Long-horizon software evolution (not single bugs).
- **Scale**: 48 tasks, avg 21 files changed, 874 regression tests per task.
- **SOTA**: GPT-5 solves only ~21% (vs 65% on SWE-bench Verified).
- **Relevance**: Highly relevant for agent evaluation.

### 8. FeatureBench (ICLR 2026)
- **What**: Feature-level development (not bug fixing).
- **Scale**: 200 tasks from 24 repos, 3,825 executable environments.
- **SOTA**: Claude 4.5 Opus at 11.0% (vs 74.4% on SWE-bench). Massive headroom.

### 9. Multi-SWE-bench
- **What**: SWE-bench expanded to 7 languages (Java, TS, JS, Go, Rust, C, C++).
- **Scale**: 1,632 instances. NeurIPS 2025.

### 10. Terminal-Bench
- **What**: Agents in real sandboxed CLI environments.
- **Tasks**: Compiling, configuring, running tools, navigating filesystems.

### Other Notable Benchmarks
- **CanAICode**: Interview-style, Docker-sandboxed, Python/JS.
- **CRUXEval/CRUXEval-X**: Code reasoning (input/output prediction), 800-12,660 subjects.
- **DevBench**: Telemetry-driven completion tasks, 1,800 instances, 6 languages.
- **CodeContests**: Competitive programming at ICPC level.
- **APPS**: 10,000 coding problems at 3 difficulty levels.
- **DS-1000**: Data science across 7 Python libraries.
- **CrossCodeEval**: Cross-file completion requiring multi-file context.
- **RepoEval**: Repo-level completion at line/API/function granularity.
- **DPAI Arena (JetBrains)**: Full developer lifecycle.
- **ContextBench**: Context retrieval quality, 1,136 tasks, 8 languages.

## What Makes a Good Agent Benchmark

| Dimension | What to Measure | Best Existing | Gap |
|---|---|---|---|
| Tool use efficiency | Calls, tokens, cost per resolved task | BFCL v4 | No benchmark combines tool efficiency with real codebase tasks |
| Multi-file navigation | Files explored vs files needed | SWE-bench, ContextBench | Navigation quality not measured separately from correctness |
| Error recovery | Success rate after first failure | Aider Polyglot | No systematic testing of different error types |
| Instruction following | Adherence to project rules | AGENTIF | No coding benchmark tests AGENTS.md-style compliance |
| Context utilization | Use of available context | ContextBench | No long-conversation context retention tests |
| Multi-step reasoning | Planning, decomposition, coherence | SWE-EVO, FeatureBench | Most benchmarks are single-step |

## AVA's Differentiators

AVA's benchmark fills the largest gap: **agent efficiency measurement**. No existing benchmark combines:
1. Cost-per-resolved-task tracking
2. Tool efficiency scoring (min expected / actual tools)
3. Harnessed-pair evaluation (SOTA director + fast worker)
4. Consistency/variance tracking across runs
5. Security and constraint-following categories
6. Multi-language coverage (Rust, Python, JS, Go)
7. Test generation evaluation (model writes tests, not just code)

The biggest opportunity: "We don't just solve problems, we solve them efficiently with fewer tool calls and lower cost."
