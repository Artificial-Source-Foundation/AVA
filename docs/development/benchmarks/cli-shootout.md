# CLI Shootout

Local benchmark harness for comparing AVA and OpenCode on the same machine.

## Goals

- Measure CLI/runtime overhead separately from model quality.
- Keep runs fair: same machine, same cwd, same prompt text, same timeout, alternating run order.
- Produce reproducible artifacts in `.tmp/benchmarks/` instead of ad-hoc terminal screenshots.

## What It Measures

### Offline (default)

- `--help` latency
- binary size capture
- peak RSS for those commands when `/usr/bin/time -v` is available

### Online (opt-in)

Enabled with `--online` and matching model flags.

- exact short reply (`BENCHMARK_OK`)
- simple repo read task (`package.json` package name)
- total wall-clock time
- time to first stdout/stderr chunk when observable
- peak RSS when available
- success rate from task-specific verifiers
- inline failure summaries in the Markdown report for unsuccessful samples

## Usage

Offline-only:

```bash
pnpm run bench:cli-shootout
```

By default the harness resolves `ava` and `opencode` from `PATH`. Override them with `--ava-bin`, `--opencode-bin`, `AVA_BENCH_BIN`, or `OPENCODE_BENCH_BIN` when needed.
Use `--ava-fast` to benchmark AVA with lower-overhead startup settings (`--fast` skips project instruction injection and eager codebase indexing).

Online with the same provider/model pair for both CLIs:

```bash
pnpm run bench:cli-shootout -- \
  --online \
  --ava-provider openrouter \
  --ava-model anthropic/claude-sonnet-4 \
  --opencode-model openrouter/anthropic/claude-sonnet-4
```

For OpenCode, make sure the chosen model string is valid for the local install (`opencode models`).

Custom binaries or fewer iterations:

```bash
node scripts/benchmarks/cli-shootout.mjs \
  --ava-bin /path/to/ava \
  --ava-fast \
  --opencode-bin /path/to/opencode \
  --offline-iterations 5 \
  --online-iterations 2
```

## Output

Each run writes:

- `.tmp/benchmarks/cli-shootout-<timestamp>.json`
- `.tmp/benchmarks/cli-shootout-<timestamp>.md`

The JSON contains per-sample raw data. The Markdown report contains median/p95 summaries per task and CLI.
The online suite defaults to 5 measured samples so failure rates and p95 values are a little less noisy than a single-shot comparison.

## Fairness Notes

- Warmup runs are discarded.
- Measured runs alternate AVA/OpenCode order to reduce cache bias.
- Offline and online numbers should be read separately.
- Online runs are only meaningful when both CLIs use the same upstream model/provider pair.
- Session state, provider auth, and network conditions can still affect online variance; use medians and repeated runs.
- Working directory matters a lot for RSS: benchmarking inside a large repo measures startup plus repo-context overhead, while a temp fixture isolates more of the pure CLI/runtime cost.
- AVA fast mode is useful for isolating prompt/context overhead. It is not the default user experience; compare both modes before drawing product conclusions.
