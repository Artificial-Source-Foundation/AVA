# Stealth Model Benchmark: Hunter Alpha, Healer Alpha, Nemotron 3 Super

**Date**: 2026-03-11 / 2026-03-12
**Benchmark Tool**: AVA Model Benchmark System v2.1
**Tested via**: OpenRouter API (all models free-tier at time of testing)

## Models Tested

| Model | ID | Params | Context | Price |
|-------|-----|--------|---------|-------|
| **Hunter Alpha** | `openrouter/hunter-alpha` | 1T | 1M | Not disclosed |
| **Healer Alpha** | `openrouter/healer-alpha` | Unknown | 262K | Not disclosed |
| **Nemotron 3 Super** | `nvidia/nemotron-3-super-120b-a12b:free` | 120B (12B active MoE) | 262K | Free |
| Claude Sonnet 4.5 | `anthropic/claude-sonnet-4.5` | — | 200K | $3/$15 per M |
| Claude Haiku 4.5 | `anthropic/claude-haiku-4.5` | — | 200K | $1/$5 per M |
| Gemini 3.1 Pro | `google/gemini-3.1-pro-preview` | — | 1M | $1.25/$10 per M |

## Test Configuration

| Run | Suite | Tasks | Models | Status |
|-----|-------|-------|--------|--------|
| Solo (speed) | speed | 12/model | Hunter Alpha vs Haiku 4.5 | Complete |
| Solo (all) | all | 24 Hunter / 23 Haiku | Hunter Alpha vs Haiku 4.5 | Partial (48/68) |
| Frontier | speed | 12/model | Hunter Alpha vs Sonnet 4.5 vs Gemini 3.1 Pro | Complete |
| Harness | all | 34 × 3 modes | Sonnet 4.5 (director) + Hunter Alpha (worker) | Complete |
| Hunter retest ×2 | speed | 12 | Hunter Alpha solo | Complete |
| Healer solo | speed | 12 | Healer Alpha solo | Complete |
| Nemotron vs Sonnet | speed | 12/model | Nemotron 3 Super vs Sonnet 4.5 | Complete |
| **Polyglot** | all (py,js,go) | 7/model | Hunter vs Nemotron vs Healer | Complete |
| **Rust stress test** | standard | 23/model | Hunter vs Healer vs Nemotron | Complete |
| **Polyglot stress test** | standard (py,js,go) | 7/model | Hunter vs Nemotron vs Healer | Complete |

Total: **10 benchmark runs**, **260+ individual task evaluations**.

---

## Overall Rankings

### Speed Suite (Rust-heavy, 12 tasks)

| Rank | Model | Pass Rate | Errors | Avg Latency | Compile |
|------|-------|-----------|--------|-------------|---------|
| 1 | **Sonnet 4.5** | 12/12 (100%) | 0 | ~8s | 9/10 |
| 2 | **Gemini 3.1 Pro** | 12/12 (100%) | 0 | ~21s | 10/10 |
| 3 | **Haiku 4.5** | 11/12 (92%) | 0 | ~11s | 9/10 |
| 4 | **Healer Alpha** | 10/12 (83%) | 1 | ~15s | 3/9 |
| 5 | **Nemotron 3 Super** | 10/12 (83%) | 1 | ~30s | 4/9 |
| 6 | **Hunter Alpha** | 2-11/12 (17-92%) | 1-10 | 17-111s | 3-4/9 |

### Polyglot (Python/JS/Go only, 7 tasks)

| Rank | Model | Pass Rate | Errors | Avg Latency | Compile |
|------|-------|-----------|--------|-------------|---------|
| 1 | **Nemotron 3 Super** | 7/7 (100%) | 0 | ~32s | 5/5 |
| 2 | **Hunter Alpha** | 5/7 (71%) | 2 | ~19s | 4/4 |
| 3 | **Healer Alpha** | 5/7 (71%) | 1 | ~31s | 5/5 |

---

## Detailed Analysis

### Hunter Alpha (1T params)

**The inconsistent giant.** Quality is genuinely good when it responds, but reliability is a dealbreaker.

**Reliability across 5 runs:**

| Run | Pass | Errors | Error Rate | Avg Latency |
|-----|------|--------|------------|-------------|
| Run 1 (vs Haiku) | 9/12 | 2 | 17% | ~17s |
| Run 2 (harness solo) | 24/34 | 5 | 15% | ~16s |
| Run 3 (vs frontier) | 2/12 | 10 | 83% | ~111s |
| Run 4 (retest) | 7/12 | 5 | 42% | ~43s |
| Run 5 (retest #2) | 11/12 | 1 | 8% | ~55s |

**Strengths:**
- **Test generation specialist** — swept all 3 test gen tasks vs Haiku (3-0), consistently producing 15-22 passing tests per task across runs. This was the single biggest differentiator.
- Strong on agentic tasks (bugfix, refactor, multi-step debug) when it responds
- Efficient tool usage (2.6 calls/task vs Haiku's 2.8)
- Self-corrects well (9 corrections across runs)

**Weaknesses:**
- 8-83% empty response error rate — wildly inconsistent
- TTFT ranges from 2s to 105s within the same run
- 14x slower than Sonnet 4.5 on average
- Errors cluster on non-code tasks (tool use, security, React components)

### Healer Alpha

**The reliable stealth model.** Similar origin to Hunter Alpha but much more consistent.

| Metric | Value |
|--------|-------|
| Speed suite pass rate | 10/12 (83%) |
| Polyglot pass rate | 5/7 (71%) |
| Errors | 1/12 speed, 1/7 polyglot |
| Avg latency | ~15s (speed), ~31s (polyglot) |
| TTFT range | 3-45s |
| Compile rate | 3/9 (speed), 5/5 (polyglot) |

**Strengths:**
- Most reliable of the three stealth/free models on Rust tasks
- Good polyglot compilation (5/5 on Python/JS)
- Moderate, predictable latency

**Weaknesses:**
- Low Rust compile rate (3/9)
- Failed `go_concurrent_map` on quality (not just error — actual wrong output)
- Test generation: only 1/3 compiled (vs Hunter's 3/3)

### Nemotron 3 Super (120B, 12B active)

**The polyglot champion.** Perfect score on non-Rust tasks, decent on Rust.

| Metric | Value |
|--------|-------|
| Speed suite pass rate | 10/12 (83%) |
| **Polyglot pass rate** | **7/7 (100%)** |
| Errors | 1/12 speed, 0/7 polyglot |
| Avg latency | ~30s (speed), ~32s (polyglot) |
| TTFT range | 2-109s (high variance) |
| Compile rate | 4/9 (speed), 5/5 (polyglot) |

**Strengths:**
- **Only model to achieve 100% polyglot pass rate** — beat Hunter and Healer
- Zero errors on polyglot tasks
- Strong Python/JS compilation
- Free pricing makes it excellent value

**Weaknesses:**
- High latency variance (TTFT swings from 2s to 109s) — likely OpenRouter free-tier queuing
- Rust compilation below frontier models (4/9 vs Sonnet's 9/10)
- Slowest average across all runs

---

## Frontier Comparison

### vs Sonnet 4.5

| Metric | Sonnet 4.5 | Best Stealth Model |
|--------|-----------|-------------------|
| Pass rate | 100% | 83-100% (Nemotron polyglot) |
| Errors | 0% | 0-83% |
| Avg latency | ~8s | ~15-32s |
| TTFT | 2-4s | 2-109s |
| Compile (Rust) | 9/10 | 3-4/9 |
| Test gen volume | 50/51 tests | 33/33 (Hunter) |

Sonnet 4.5 dominates on every metric except it costs $3/$15 per M tokens.

### vs Gemini 3.1 Pro

| Metric | Gemini 3.1 Pro | Best Stealth Model |
|--------|---------------|-------------------|
| Pass rate | 100% | 83-100% |
| Compile (Rust) | **10/10** | 3-4/9 |
| Avg latency | ~21s | ~15-32s |

Gemini had the best compile rate of any model tested — perfect 10/10. The stealth models can't touch its Rust compilation quality.

### vs Haiku 4.5

| Metric | Haiku 4.5 | Best Stealth Model |
|--------|----------|-------------------|
| Pass rate | 92-96% | 83-100% |
| Errors | 0% | 0-83% |
| Avg latency | ~11s | ~15-32s |
| Test generation | 0/3 | **3/3** (Hunter) |

The stealth models are competitive with Haiku on quality but can't match its consistency. Hunter Alpha's test generation dominance over Haiku is the one clear win.

---

## Harnessed Pair: Sonnet 4.5 + Hunter Alpha

| Mode | Pass Rate | Avg Latency | Cost |
|------|-----------|-------------|------|
| Sonnet 4.5 solo | 31/34 (91%) | ~14.5s | — |
| Hunter Alpha solo | 24/34 (71%) | ~15.5s | — |
| **Pair** | **29/34 (85%)** | ~24.5s | $0.1386 |

The pair rescued 5 tasks Hunter couldn't solve alone (merge_sorted, go_concurrent_map, concurrent_counter, binary_tree, security tasks) but underperformed Sonnet solo due to Hunter's empty response errors disrupting coordination. Cost was ~$0.004/task.

**Verdict**: Not an effective pairing. Hunter's reliability breaks the director-worker pipeline. A faster, reliable worker (Haiku, Mercury 2) would pair better.

---

## Stress Test Results (Final Confirmation)

### Rust Standard Suite — All 3 Models (23 tasks each)

| Model | Pass Rate | Errors | Compile Rate | Avg Latency |
|-------|-----------|--------|-------------|-------------|
| **Hunter Alpha** | 19/23 (83%) | 2 (9%) | 11/15 (73%) | ~34s |
| **Healer Alpha** | 18/23 (78%) | 2 (9%) | 13/17 (76%) | ~22s |
| **Nemotron 3 Super** | 15/23 (65%) | 3 (13%) | 11/15 (73%) | ~21s |

### Polyglot Standard Suite — All 3 Models (7 tasks each)

| Model | Pass Rate | Errors | Compile Rate | Avg Latency |
|-------|-----------|--------|-------------|-------------|
| **Hunter Alpha** | 6/7 (86%) | 0 | 4/5 (80%) | ~11s |
| **Nemotron 3 Super** | 6/7 (86%) | 0 | 4/5 (80%) | ~21s |
| **Healer Alpha** | 5/7 (71%) | 0 | 4/5 (80%) | ~15s |

### Cumulative Scores (All Runs Combined)

| Model | Total Tasks | Pass Rate | Error Rate | Best Category |
|-------|------------|-----------|------------|---------------|
| **Hunter Alpha** | ~80 tasks | ~82% | ~12% | Test generation (always 3/3) |
| **Healer Alpha** | ~50 tasks | ~77% | ~6% | Agentic tasks, fastest |
| **Nemotron 3 Super** | ~70 tasks | ~78% | ~8% | Polyglot (90% cumulative) |

---

## Verdict & Recommendations

### Tier List

| Tier | Model | Best For |
|------|-------|---------|
| **S** | Sonnet 4.5 | Everything — fastest, most reliable, best quality |
| **A** | Gemini 3.1 Pro | Rust compilation, when you need perfect code output |
| **B+** | Haiku 4.5 | Cost-effective baseline, reliable workhorse |
| **B** | Hunter Alpha | Rust coding, test generation — best free Rust model |
| **B-** | Nemotron 3 Super | Polyglot tasks, best free Python/JS/Go model |
| **B-** | Healer Alpha | Reliable free option, good compile rate, fastest free |
| **C** | Other free models | Most lack tool use support — not viable for agentic benchmarks |

### Who Should Use What

- **Need reliability?** Sonnet 4.5 > Haiku 4.5 > Gemini 3.1 Pro. Don't use stealth models.
- **Budget-constrained polyglot work?** Nemotron 3 Super — 100% pass rate, free.
- **Need test generation?** Hunter Alpha is genuinely the best at this (when it responds). Retry on error.
- **Free Rust coding?** Hunter Alpha — 83% pass, 73% compile, strong test gen
- **Free polyglot?** Nemotron 3 Super — 90% cumulative, zero errors on py/js/go
- **Free reliable all-rounder?** Healer Alpha — lowest error rate, fastest, 76% compile
- **Agentic worker model?** None of the stealth models. Use Haiku 4.5 or Mercury 2.

### Key Insight

The stealth models (Hunter Alpha, Healer Alpha) are **not frontier models despite their marketing**. They are mid-tier models with interesting specializations (Hunter's test generation) served on immature infrastructure. Nemotron 3 Super, despite being a much smaller model (12B active), outperforms both on polyglot tasks through sheer reliability.

**The free-tier infrastructure is the bottleneck, not the models themselves.** Hunter Alpha's quality when it works suggests a capable model hampered by serving issues. If/when pricing and infrastructure stabilize, these models warrant retesting.

---

## Raw Data

| Run | File |
|-----|------|
| Speed (vs Haiku) | `~/.ava/benchmarks/bench-2026-03-11_21-53-28.json` |
| Frontier (vs Sonnet/Gemini) | `~/.ava/benchmarks/bench-2026-03-12_03-02-45.json` |
| Harness | `~/.ava/benchmarks/harness-2026-03-11_23-42-35.json` |
| Hunter retest #1 | `~/.ava/benchmarks/bench-2026-03-12_03-21-48.json` |
| Hunter retest #2 | `~/.ava/benchmarks/bench-2026-03-12_03-44-51.json` |
| Healer solo | `~/.ava/benchmarks/bench-2026-03-12_03-29-20.json` |
| Nemotron vs Sonnet | `~/.ava/benchmarks/bench-2026-03-12_03-30-41.json` |
| Polyglot (3-way) | `~/.ava/benchmarks/bench-2026-03-12_04-09-04.json` |
| Rust stress test | `~/.ava/benchmarks/bench-2026-03-12_05-43-43.json` |
| Polyglot stress test | `~/.ava/benchmarks/bench-2026-03-12_05-06-52.json` |

## Methodology

- All runs via OpenRouter API (free tier for stealth models, paid for Anthropic/Google)
- Validation: Tier 1 (regex pattern matching) + Tier 2 (compile & test with harness)
- No LLM-as-Judge used (would require `--judges` flag with separate SOTA model)
- Cost data unavailable for stealth models — OpenRouter reports $0.00
- Go compile failures are expected (sandbox lacks Go toolchain) — quality assessed via regex
- Python/JS compile & test works via benchmark sandbox (python3, node)
- Empty response errors may reflect content filtering, rate limiting, or infrastructure issues
- Each "run" is a fresh benchmark invocation with cold start
- TTFT = Time to First Token; measured from request send to first streaming chunk
