# Sprint 19: Send-Safe Agent Stack & Headless CLI Mode

> For AI coding agent. Estimated: 4 features, mix S/M effort.
> Run `cargo test --workspace` after each feature.
> Depends on: Sprint 16a, 16b, 16c (all complete)

---

## Role

You are implementing Sprint 19 (Send-Safe Agent Stack & Headless CLI) for AVA, a multi-agent AI coding assistant.

Read these files first:
- `CLAUDE.md` (conventions, Rust-first architecture)
- `AGENTS.md` (code standards)
- `crates/ava-agent/src/stack.rs` (AgentStack — currently not Send)
- `crates/ava-memory/src/lib.rs` (MemorySystem — holds raw rusqlite::Connection)
- `crates/ava-session/src/lib.rs` (SessionManager — already Send, stores db_path not Connection)
- `crates/ava-tui/src/app.rs` (TUI app — requires TTY)
- `crates/ava-tui/src/main.rs` (entry point)
- `crates/ava-tui/src/bin/smoke.rs` (smoke test — workaround for non-Send)

**Context**: Two blockers found during smoke testing:

1. **`AgentStack` is not `Send`** because `MemorySystem` holds a raw `rusqlite::Connection` (which uses `RefCell` internally). This prevents `tokio::spawn()` — you can't run the agent on a background task. The TUI works around this with `LocalSet`, but it's fragile and limits concurrency.

2. **The `ava` binary always enters TUI mode** (raw terminal). There's no headless/batch mode for CI, piped output, or non-interactive use. Running `ava "goal"` should work without a TTY.

**Why**: Without Send-safe AgentStack, the TUI can't spawn agent work on background tasks, and multi-agent (Praxis) can't run agents concurrently. Without headless mode, the CLI is useless in scripts, CI, and piped workflows.

---

## Pre-Implementation: Read Existing Code

Before writing any code, read:
- `crates/ava-memory/src/lib.rs` — MemorySystem (the non-Send culprit)
- `crates/ava-agent/src/stack.rs` — AgentStack composition
- `crates/ava-db/src/lib.rs` — Database pool (may already have Send-safe patterns)
- `crates/ava-session/src/lib.rs` — SessionManager (Send-safe pattern to follow)
- `crates/ava-tui/src/app.rs` — TUI event loop (uses LocalSet workaround)
- `crates/ava-tui/src/main.rs` — Entry point
- `crates/ava-tui/src/config/cli.rs` — CLI args

---

## Feature 1: Make MemorySystem Send-Safe

### What to Build
Refactor `MemorySystem` to not hold a persistent `rusqlite::Connection`, matching the pattern used by `SessionManager`.

**File:** `crates/ava-memory/src/lib.rs` (modify)

**Current (broken):**
```rust
pub struct MemorySystem {
    conn: Connection,  // RefCell inside — not Send, not Sync
}
```

**Target (two options, pick the simpler one that works):**

**Option A — Store path, open connection per call (like SessionManager):**
```rust
pub struct MemorySystem {
    db_path: PathBuf,
}

impl MemorySystem {
    pub fn new(path: impl AsRef<Path>) -> Result<Self> {
        let system = Self { db_path: path.as_ref().to_path_buf() };
        system.init_schema()?;
        Ok(system)
    }

    fn conn(&self) -> Result<Connection> {
        Connection::open(&self.db_path).map_err(|e| /* ... */)
    }

    pub fn remember(&self, key: &str, value: &str) -> Result<Memory> {
        let conn = self.conn()?;
        // ... use conn
    }
    // ... same for recall, search, get_recent
}
```

**Option B — Wrap in Mutex (if connection pooling is needed):**
```rust
use std::sync::Mutex;

pub struct MemorySystem {
    conn: Mutex<Connection>,
}
```

**Recommendation:** Use Option A (path-based, like SessionManager). It's simpler, proven to work, and memory operations are infrequent enough that opening a connection per call is fine. SQLite's WAL mode handles concurrent readers well.

### Verification
After this change:
- `MemorySystem` must be `Send + Sync`
- All existing tests in `crates/ava-memory/` must still pass
- Add a compile-time assertion:
  ```rust
  #[cfg(test)]
  const _: () = {
      fn assert_send<T: Send + Sync>() {}
      fn check() { assert_send::<MemorySystem>(); }
  };
  ```

### Tests
- All existing tests pass (remember, recall, search, get_recent, persistence)
- New: compile-time Send+Sync assertion
- New: test concurrent access from two threads

---

## Feature 2: Verify AgentStack is Send

### What to Build
After Feature 1, `AgentStack` should automatically become Send. Verify and add compile-time assertions.

**File:** `crates/ava-agent/src/stack.rs` (modify)

Add at the bottom:
```rust
#[cfg(test)]
const _: () = {
    fn assert_send<T: Send>() {}
    fn check() { assert_send::<AgentStack>(); }
};
```

If this fails, there are other non-Send fields. Check each field:
- `ModelRouter` — uses `Arc<RwLock<...>>`, should be Send
- `Arc<ToolRegistry>` — check if ToolRegistry is Send
- `SessionManager` — stores PathBuf, is Send
- `MemorySystem` — now Send (Feature 1)
- `ConfigManager` — uses `Arc<RwLock<...>>`, should be Send
- `Arc<StandardPlatform>` — check if Send

Fix any remaining non-Send fields using the same pattern (Mutex or path-based).

### Update smoke test
Once AgentStack is Send, update the smoke test to use `tokio::spawn`:

**File:** `crates/ava-tui/src/bin/smoke.rs` (modify)

```rust
// This should now compile:
let handle = tokio::spawn(async move {
    stack.run("Say hello", 1, Some(tx), cancel).await
});
```

### Update TUI app
Remove the `LocalSet` workaround in `app.rs` if it was only there because of non-Send AgentStack.

### Tests
- Compile-time Send assertion for AgentStack
- Smoke test with `tokio::spawn` passes
- `cargo test --workspace` all green

---

## Feature 3: Headless / Batch CLI Mode

### What to Build
When the `ava` binary detects it's not running in an interactive terminal (no TTY), or when explicit flags are passed, run in headless mode — no TUI, just streaming text output to stdout.

**File:** `crates/ava-tui/src/main.rs` (modify)

```rust
use std::io::IsTerminal;

#[tokio::main]
async fn main() -> Result<()> {
    color_eyre::install()?;
    // ... tracing setup ...

    let cli = CliArgs::parse();

    // Headless mode: no TTY, or --json flag, or piped stdin
    if !std::io::stdout().is_terminal() || cli.json || cli.headless {
        return run_headless(cli).await;
    }

    // Interactive TUI mode
    let mut app = App::new(cli).await?;
    app.run().await
}
```

**File:** `crates/ava-tui/src/config/cli.rs` (modify)

Add flags:
```rust
/// Output JSON events (for scripting/piping)
#[arg(long)]
pub json: bool,

/// Force headless mode (no TUI)
#[arg(long)]
pub headless: bool,
```

**File:** `crates/ava-tui/src/headless.rs` (new)

```rust
use crate::config::cli::CliArgs;
use ava_agent::stack::{AgentStack, AgentStackConfig};
use ava_agent::AgentEvent;
use color_eyre::eyre::{eyre, Result};
use tokio::sync::mpsc;
use tokio_util::sync::CancellationToken;

pub async fn run_headless(cli: CliArgs) -> Result<()> {
    let goal = cli.goal.ok_or_else(|| eyre!("No goal provided. Usage: ava \"your goal here\""))?;

    let data_dir = dirs::home_dir()
        .unwrap_or_default()
        .join(".ava");

    let stack = AgentStack::new(AgentStackConfig {
        data_dir,
        provider: cli.provider,
        model: cli.model,
        max_turns: cli.max_turns,
        yolo: cli.yolo,
        ..Default::default()
    }).await?;

    let (tx, mut rx) = mpsc::unbounded_channel();
    let cancel = CancellationToken::new();

    // Handle Ctrl+C
    let cancel_clone = cancel.clone();
    tokio::spawn(async move {
        tokio::signal::ctrl_c().await.ok();
        eprintln!("\nAborting...");
        cancel_clone.cancel();
    });

    // Spawn agent (requires Send-safe AgentStack from Feature 2)
    let handle = tokio::spawn(async move {
        stack.run(&goal, cli.max_turns, Some(tx), cancel).await
    });

    // Stream output
    if cli.json {
        // JSON mode: one event per line
        while let Some(event) = rx.recv().await {
            let json = match &event {
                AgentEvent::Token(t) => serde_json::json!({"type": "token", "content": t}),
                AgentEvent::ToolCall(tc) => serde_json::json!({"type": "tool_call", "tool": format!("{:?}", tc)}),
                AgentEvent::ToolResult(tr) => serde_json::json!({"type": "tool_result", "result": format!("{:?}", tr)}),
                AgentEvent::Progress(p) => serde_json::json!({"type": "progress", "message": p}),
                AgentEvent::Complete(_) => serde_json::json!({"type": "complete"}),
                AgentEvent::Error(e) => serde_json::json!({"type": "error", "message": e}),
            };
            println!("{}", json);
        }
    } else {
        // Text mode: stream tokens to stdout
        while let Some(event) = rx.recv().await {
            match &event {
                AgentEvent::Token(t) => print!("{t}"),
                AgentEvent::ToolCall(tc) => eprintln!("[tool: {:?}]", tc),
                AgentEvent::ToolResult(tr) => eprintln!("[result: {:?}]", tr),
                AgentEvent::Progress(p) => eprintln!("[{p}]"),
                AgentEvent::Complete(_) => break,
                AgentEvent::Error(e) => {
                    eprintln!("[error: {e}]");
                    break;
                }
            }
        }
        println!(); // final newline
    }

    let result = handle.await??;

    if cli.json {
        println!("{}", serde_json::json!({
            "type": "summary",
            "success": result.success,
            "turns": result.turns,
        }));
    } else {
        eprintln!("[Done] success={}, turns={}", result.success, result.turns);
    }

    std::process::exit(if result.success { 0 } else { 1 });
}
```

**Behavior:**
- `ava "fix the bug"` — if TTY, opens TUI and auto-submits. If piped, runs headless.
- `ava "fix the bug" --headless` — forces headless even with TTY
- `ava "fix the bug" --json` — headless with JSON event stream (for scripting)
- `echo "fix the bug" | ava` — not supported initially (goal must be CLI arg)
- Ctrl+C cancels gracefully via CancellationToken
- Tokens stream to stdout, tool calls/progress to stderr (so stdout is clean output)
- Exit code: 0 on success, 1 on failure

### Tests
- Headless mode with mock provider completes
- JSON mode outputs valid JSON per line
- Missing goal in headless returns clear error
- Ctrl+C triggers cancellation

---

## Feature 4: Clean Up Smoke Test

### What to Build
Now that headless mode exists, the smoke test binary is redundant for real-provider testing. Simplify it to just the mock test and point users to `ava --headless` for real testing.

**File:** `crates/ava-tui/src/bin/smoke.rs` (simplify)

Keep only the mock provider test. Remove `--real` flag. Add a note:
```rust
// For real provider testing, use:
//   cargo run --bin ava -- "Say hello" --headless --provider openrouter --model anthropic/claude-sonnet-4
```

### Tests
- Mock smoke test still passes
- `cargo run --bin ava -- "Say hello" --headless --provider openrouter --model anthropic/claude-sonnet-4` works

---

## Post-Implementation Verification

After ALL 4 features:

1. `cargo test --workspace` — all tests pass
2. `cargo clippy --workspace` — no warnings
3. Compile-time: `AgentStack: Send` assertion compiles
4. `cargo run --bin ava-smoke` — mock test passes
5. `cargo run --bin ava -- "Say hello in one sentence" --headless --provider openrouter --model anthropic/claude-sonnet-4` — real API call works
6. `cargo run --bin ava -- "Say hello" --headless --json --provider openrouter --model anthropic/claude-sonnet-4` — JSON output valid
7. `cargo run --bin ava -- --help` — shows new flags
8. Commit: `git commit -m "feat(sprint-19): send-safe agent stack and headless CLI mode"`

---

## File Change Summary

| Action | File |
|--------|------|
| MODIFY | `crates/ava-memory/src/lib.rs` (path-based Connection, Send+Sync) |
| MODIFY | `crates/ava-agent/src/stack.rs` (Send assertion, remove workarounds) |
| MODIFY | `crates/ava-tui/src/main.rs` (headless routing) |
| MODIFY | `crates/ava-tui/src/config/cli.rs` (add --json, --headless flags) |
| MODIFY | `crates/ava-tui/src/app.rs` (remove LocalSet workaround if applicable) |
| CREATE | `crates/ava-tui/src/headless.rs` (headless runner) |
| MODIFY | `crates/ava-tui/src/lib.rs` (add pub mod headless) |
| MODIFY | `crates/ava-tui/src/bin/smoke.rs` (simplify, remove --real) |
