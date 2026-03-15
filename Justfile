# AVA Development Commands

default:
    @just --list

# Run all checks (format + lint + test)
check:
    cargo fmt --all --check
    cargo clippy --workspace -- -D warnings
    cargo nextest run --workspace

# Run tests
test *ARGS:
    cargo nextest run --workspace {{ ARGS }}

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

# Clean build artifacts
clean:
    cargo clean
    sccache --zero-stats 2>/dev/null || true
