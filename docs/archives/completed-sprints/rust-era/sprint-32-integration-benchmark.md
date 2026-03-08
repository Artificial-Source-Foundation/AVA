# Sprint 32: Integration Testing & Performance Benchmark

> **Hands-on sprint** — run with the project lead, not fully autonomous.

## Goal

Verify the full AVA stack works end-to-end: TUI, desktop integration, all tools, and benchmark performance against OpenCode (TypeScript competitor).

## Part 1: TUI End-to-End Test

### 1.1 Interactive TUI Boot
```bash
cargo run --bin ava -- --provider openrouter --model anthropic/claude-sonnet-4
```

Verify:
- [ ] TUI boots without crash
- [ ] Status bar shows provider/model
- [ ] Can type a message and press Enter
- [ ] Agent streams tokens incrementally (not all at once)
- [ ] Streaming cursor visible during generation
- [ ] Tool calls show in UI with approval modal (not --yolo)
- [ ] Approve a tool, see result
- [ ] Agent completes, UI returns to input
- [ ] Ctrl+C cancels running agent
- [ ] Ctrl+Q quits cleanly

### 1.2 Headless Mode
```bash
cargo run --bin ava -- "List the files in the current directory" --headless --provider openrouter --model anthropic/claude-sonnet-4
```

Verify:
- [ ] Agent calls glob or bash tool
- [ ] Tool result printed to stderr
- [ ] Final answer printed to stdout
- [ ] Exit code 0 on success

### 1.3 JSON Mode
```bash
cargo run --bin ava -- "What is 2+2?" --headless --json --provider openrouter --model anthropic/claude-sonnet-4
```

Verify:
- [ ] NDJSON output (one JSON object per line)
- [ ] Token events, tool events, complete event all present
- [ ] Parseable by `jq`

### 1.4 Multi-Agent Mode
```bash
cargo run --bin ava -- "Explain what this project does" --headless --multi-agent --provider openrouter --model anthropic/claude-sonnet-4
```

Verify:
- [ ] Commander starts workers
- [ ] Worker events printed
- [ ] Summary event at end

## Part 2: Desktop Integration Smoke Test

### 2.1 Tauri Build
```bash
npm run tauri dev
```

Verify:
- [ ] Desktop app launches
- [ ] Chat interface renders
- [ ] Can send a message (if provider configured)
- [ ] Rust backend responds via Tauri commands

### 2.2 Bridge Verification

Check that `src-tauri/src/commands/` still compiles and the command signatures match what the TypeScript frontend expects. Key commands to verify:
- [ ] `generate` / `chat` command exists and calls into ava-llm
- [ ] `list_tools` command works
- [ ] `session` commands work (list, create, load)

If the desktop bridge is broken after sprint refactoring, document what broke and what needs fixing (separate sprint).

## Part 3: Tool Smoke Test

Run each core tool via headless mode and verify:

### 3.1 Read Tool
```bash
cargo run --bin ava -- "Read the file CLAUDE.md and tell me the first 3 lines" --headless --yolo --provider openrouter --model anthropic/claude-sonnet-4
```
- [ ] Agent calls read tool
- [ ] Returns file contents
- [ ] Agent summarizes correctly

### 3.2 Write Tool
```bash
cargo run --bin ava -- "Create a file /tmp/ava-test.txt with the content 'hello from ava'" --headless --yolo --provider openrouter --model anthropic/claude-sonnet-4
cat /tmp/ava-test.txt
```
- [ ] File created
- [ ] Content correct

### 3.3 Edit Tool
```bash
echo "hello world" > /tmp/ava-edit-test.txt
cargo run --bin ava -- "Edit /tmp/ava-edit-test.txt and change 'world' to 'ava'" --headless --yolo --provider openrouter --model anthropic/claude-sonnet-4
cat /tmp/ava-edit-test.txt
```
- [ ] File edited correctly

### 3.4 Bash Tool
```bash
cargo run --bin ava -- "Run 'uname -a' and tell me the OS" --headless --yolo --provider openrouter --model anthropic/claude-sonnet-4
```
- [ ] Bash command executed
- [ ] Result returned to agent

### 3.5 Glob Tool
```bash
cargo run --bin ava -- "Find all Cargo.toml files in this project" --headless --yolo --provider openrouter --model anthropic/claude-sonnet-4
```
- [ ] Glob pattern executed
- [ ] Returns list of Cargo.toml files

### 3.6 Grep Tool
```bash
cargo run --bin ava -- "Search for 'LLMProvider' in crates/ava-llm/src/" --headless --yolo --provider openrouter --model anthropic/claude-sonnet-4
```
- [ ] Grep executed
- [ ] Returns matching lines

## Part 4: Performance Benchmark vs OpenCode

### 4.1 Setup
```bash
# Build AVA release binary
cargo build --release -p ava-tui

# Install OpenCode (if not already)
npm install -g @anthropic/opencode  # or however OC is installed

# Prepare test workspace
mkdir -p /tmp/ava-benchmark && cd /tmp/ava-benchmark
git init && echo "test" > README.md && git add . && git commit -m "init"
```

### 4.2 Cold Start Time
```bash
# AVA
time target/release/ava --help

# OpenCode
time opencode --help
```

Expected: AVA should be 10-50x faster (native binary vs Node.js startup)

### 4.3 Time-to-First-Token
```bash
# AVA (measure time from launch to first streaming token)
time cargo run --release --bin ava -- "Say hello" --headless --provider openrouter --model anthropic/claude-sonnet-4 2>&1 | head -1

# OpenCode (same test)
time opencode "Say hello" 2>&1 | head -1
```

### 4.4 Tool Execution Speed
```bash
# Create a large test file
python3 -c "print('\n'.join([f'line {i}: ' + 'x'*100 for i in range(10000)]))" > /tmp/ava-benchmark/large.txt

# AVA: read large file
time cargo run --release --bin ava -- "Read /tmp/ava-benchmark/large.txt and count the lines" --headless --yolo --provider openrouter --model anthropic/claude-sonnet-4

# AVA: grep across codebase
time cargo run --release --bin ava -- "Search for 'fn main' across all .rs files in this project" --headless --yolo --provider openrouter --model anthropic/claude-sonnet-4
```

### 4.5 Memory Usage
```bash
# AVA
/usr/bin/time -v target/release/ava "Say hello" --headless --provider openrouter --model anthropic/claude-sonnet-4 2>&1 | grep "Maximum resident"

# OpenCode
/usr/bin/time -v opencode "Say hello" 2>&1 | grep "Maximum resident"
```

Expected: AVA ~15-30MB, OpenCode ~80-150MB (Node.js baseline)

### 4.6 Binary Size
```bash
ls -lh target/release/ava
# Compare with OpenCode's node_modules size
du -sh $(which opencode)/../../lib/node_modules/@anthropic/opencode/
```

## Part 5: Results Template

Create `docs/benchmarks/benchmark-2026-03.md`:

```markdown
# AVA vs OpenCode Performance Benchmark — 2026-03

## Environment
- OS: [uname -a]
- CPU: [lscpu | grep "Model name"]
- RAM: [free -h]
- AVA version: [git rev-parse --short HEAD]
- OpenCode version: [opencode --version]
- Provider: OpenRouter / anthropic/claude-sonnet-4

## Results

| Metric | AVA (Rust) | OpenCode (TypeScript) | Ratio |
|--------|-----------|----------------------|-------|
| Cold start | Xms | Xms | Xx faster |
| Time-to-first-token | Xs | Xs | Xx faster |
| Memory (peak RSS) | XMB | XMB | Xx less |
| Binary size | XMB | XMB | Xx smaller |
| File read (10K lines) | Xms | Xms | Xx faster |
| Grep (codebase) | Xms | Xms | Xx faster |

## Tool Test Results
| Tool | Status | Notes |
|------|--------|-------|
| read | pass/fail | |
| write | pass/fail | |
| edit | pass/fail | |
| bash | pass/fail | |
| glob | pass/fail | |
| grep | pass/fail | |

## Desktop Integration
| Component | Status | Notes |
|-----------|--------|-------|
| Tauri build | pass/fail | |
| Chat command | pass/fail | |
| Session commands | pass/fail | |

## Issues Found
[List any bugs or regressions]
```

## Constraints

- This sprint is **interactive** — run with the project lead
- Document everything in the benchmark report
- Fix critical bugs found during testing (file separate issues for non-critical)
- Do NOT modify test setup to make benchmarks look better
