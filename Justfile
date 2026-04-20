# AVA Development Commands

hook_entrypoint := "bash scripts/dev/git-hooks.sh"
rust_throttle := "bash scripts/dev/run-rust-throttled.sh"

default:
    @just --list

# Run the pragmatic local Rust confidence gate
check:
    {{hook_entrypoint}} check

# Run the staged-file pre-commit checks used by lefthook
hook-pre-commit:
    {{hook_entrypoint}} pre-commit

# Run the path-aware pre-push gate used by lefthook
hook-pre-push:
    {{hook_entrypoint}} pre-push

# Run tests (per-crate to avoid OOM)
test *ARGS:
    cargo nextest run -j 4 {{ ARGS }}

# Run ALL workspace tests (CI only — uses lots of RAM)
test-all:
    cargo nextest run --workspace -j 4

# Run clippy
lint:
    cargo clippy --workspace -- -D warnings

# Format code
fmt:
    cargo fmt --all

# Run the TUI
run *ARGS:
    cargo run --bin ava -- {{ ARGS }}

# Run headless mode
headless GOAL *ARGS:
    cargo run --bin ava -- "{{ GOAL }}" --headless {{ ARGS }}

# Run TUI in release mode (optimized, much faster)
run-release *ARGS:
    cargo run --release --bin ava -- {{ ARGS }}

# Run headless in release mode
headless-release GOAL *ARGS:
    cargo run --release --bin ava -- "{{ GOAL }}" --headless {{ ARGS }}

# Run desktop app in release mode (optimized Rust + debug webview)
tauri-release:
    npm run tauri build -- --debug

# Build release binary
build-release:
    cargo build --release --bin ava

# Check documentation builds
doc:
    RUSTDOCFLAGS="-D warnings" cargo doc --workspace --no-deps

# Open documentation in browser
doc-open:
    RUSTDOCFLAGS="-D warnings" cargo doc --workspace --no-deps --open

# Run cargo-deny checks
deny:
    cargo deny check

# Run coverage (requires cargo-llvm-cov)
cov:
    cargo llvm-cov nextest --workspace --html
    @echo "Coverage report: target/llvm-cov/html/index.html"

# Find unused dependencies (requires cargo-machete)
machete:
    cargo machete

# Run benchmarks
bench *ARGS:
    cargo bench --workspace {{ ARGS }}

# Compare local AVA and OpenCode CLI speed
bench-cli-shootout *ARGS:
    node scripts/benchmarks/cli-shootout.mjs {{ ARGS }}

# Quick smoke test (headless, fast)
smoke:
    cargo run --bin ava -- "Reply with SMOKE_OK" --headless --provider openai --model gpt-5.4 --max-turns 2 --auto-approve

# Run the backend automation gate (no-secrets required, live-provider optional)
backend-gate:
    bash scripts/testing/backend-automation-gate.sh

# Run the local V1 preflight aggregate sequence (not wired to CI/hooks)
v1-gate:
     bash scripts/testing/verify-v1.sh

# Run benchmark-backed headless V1 signoff (authoritative V1-evals proof path)
v1-signoff:
     bash scripts/testing/signoff-v1-headless.sh

# Run focused regression coverage for V1 signoff branch logic
v1-signoff-regression:
     bash scripts/testing/signoff-v1-regression.sh

# Install AVA locally
install:
    cargo install --path crates/ava-tui --bin ava

# Broader local verification pass; CI remains authoritative
ci: check
    {{rust_throttle}} cargo nextest run --workspace -j 4
    RUSTDOCFLAGS="-D warnings" cargo doc --workspace --no-deps
    pnpm typecheck
    pnpm lint

# Clean build artifacts
clean:
    cargo clean
    sccache --zero-stats 2>/dev/null || true
