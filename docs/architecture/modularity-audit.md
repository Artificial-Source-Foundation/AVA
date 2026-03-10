# Modularity Audit — AVA Rust Codebase

> Generated: 2026-03-08 | Scope: all 22 crates, 229 `.rs` files, ~55,500 lines

---

## Summary

| Metric | Count |
|--------|-------|
| Crates | 22 |
| Source files | 229 |
| Total lines (Rust) | ~55,500 |
| Files > 500 lines | 8 |
| Files 300–500 lines | 22+ |
| God structs (>10 fields) | 6 |
| Circular dependencies | 0 (confirmed) |
| Flat dirs with 8+ files | 5 |

**Health verdict:** The codebase is generally well-modularized. The dependency
graph is a clean DAG with 8 leaf crates (no ava-* deps). The main concerns are:
a handful of oversized files mixing multiple responsibilities, 6 god structs
concentrated in `ava-tui` and `ava-agent`, and a few flat directories that could
benefit from sub-modules.

---

## 1. Large Files (>300 lines) — Split Recommendations

### Tier 1: >500 lines (must split)

#### `ava-config/src/model_catalog.rs` — 1,075 lines ⚠️ LARGEST

**Responsibilities:** Catalog types (`CatalogModel`, `ModelCatalog`) + API
fetching + merge logic + `fallback_catalog()` (~350 lines of hardcoded model
data) + tests (~250 lines).

**Split plan:**
| Extract to | Contents | Effort |
|-----------|----------|--------|
| `model_catalog/types.rs` | `CatalogModel`, `CatalogState`, `ModelCapability` structs | trivial |
| `model_catalog/fallback.rs` | `fallback_catalog()` — pure data, ~350 lines | trivial |
| `model_catalog/fetch.rs` | `fetch_from_api()`, merge logic | moderate |
| `model_catalog/mod.rs` | Re-exports only | trivial |

#### `ava-tui/src/app/modals.rs` — 713 lines

**Responsibilities:** 6 modal handler functions, each ~80–120 lines:
`handle_help_modal`, `handle_settings_modal`, `handle_model_selector_modal`,
`handle_session_list_modal`, `handle_auth_modal`, `handle_tools_modal`.

**Split plan:**
| Extract to | Contents | Effort |
|-----------|----------|--------|
| `modals/help.rs` | `handle_help_modal` | trivial |
| `modals/settings.rs` | `handle_settings_modal` | trivial |
| `modals/model_selector.rs` | `handle_model_selector_modal` | trivial |
| `modals/session_list.rs` | `handle_session_list_modal` | trivial |
| `modals/auth.rs` | `handle_auth_modal` | trivial |
| `modals/tools.rs` | `handle_tools_modal` | trivial |
| `modals/mod.rs` | Re-exports | trivial |

#### `ava-agent/src/agent_loop/mod.rs` — 659 lines

**Responsibilities:** `AgentLoop` struct + `run()` (~200 lines) + `run_streaming()`
(~200 lines, near-duplicate of `run()`) + tool execution + history management.

**Split plan:**
| Extract to | Contents | Effort |
|-----------|----------|--------|
| `agent_loop/streaming.rs` | `run_streaming()` + streaming-specific helpers | moderate |
| `agent_loop/execution.rs` | `execute_tool()`, `process_tool_calls()` | moderate |
| Refactor | DRY up `run()`/`run_streaming()` shared logic into common helper | significant |

#### `ava-commander/src/workflow.rs` — 652 lines

**Responsibilities:** `Workflow` types + `WorkflowExecutor` + prompt templates
(~150 lines of inline strings) + preset definitions (`default_workflow()`,
`review_workflow()`) + tests.

**Split plan:**
| Extract to | Contents | Effort |
|-----------|----------|--------|
| `workflow/presets.rs` | `default_workflow()`, `review_workflow()` + preset data | trivial |
| `workflow/prompts.rs` | Prompt template strings | trivial |
| `workflow/executor.rs` | `WorkflowExecutor` implementation | moderate |
| `workflow/types.rs` | `Workflow`, `Phase`, `PhaseRole` structs | trivial |

#### `ava-commander/src/review.rs` — 637 lines

**Responsibilities:** Review types (`ReviewContext`, `ReviewResult`, `Severity`,
`ReviewVerdict`) + diff collection (`collect_git_diff`) + prompt building +
output parsing + formatting + agent runner.

**Split plan:**
| Extract to | Contents | Effort |
|-----------|----------|--------|
| `review/types.rs` | `ReviewContext`, `ReviewResult`, `Severity`, `ReviewVerdict`, `DiffMode` | trivial |
| `review/diff.rs` | `collect_git_diff()`, diff-related helpers | moderate |
| `review/prompt.rs` | Prompt construction and output parsing | moderate |
| `review/runner.rs` | `run_review()` agent execution | moderate |

#### `ava-agent/src/stack.rs` — 625 lines

**Responsibilities:** `AgentStack` god struct (17 fields) doing initialization +
MCP management + tool registry + model switching + run orchestration.

**Split plan:**
| Extract to | Contents | Effort |
|-----------|----------|--------|
| `stack/mcp.rs` | MCP server init, `init_mcp_servers()`, `shutdown_mcp()` | moderate |
| `stack/tools.rs` | Tool registration, `register_custom_tools()` | moderate |
| `stack/builder.rs` | `AgentStackBuilder` (replace current new() with builder) | significant |

#### `ava-tui/src/app/mod.rs` — 563 lines

**Responsibilities:** `AppState` god struct (17 fields) + `App::new()` init +
event dispatch + modal state + message handling + lifecycle.

**Split plan:**
| Extract to | Contents | Effort |
|-----------|----------|--------|
| `app/init.rs` | `App::new()`, initialization logic | moderate |
| `app/state.rs` | `AppState` struct + state accessors | moderate |
| Consider | Decompose `AppState` into sub-states (see God Structs section) | significant |

#### `ava-permissions/src/inspector.rs` — 563 lines

**Assessment:** ~220 lines logic + ~340 lines tests. The logic portion is within
bounds. **No split needed** — tests inflate the count but are co-located by
convention.

### Tier 2: 300–500 lines (review needed)

| File | Lines | Assessment |
|------|-------|------------|
| `ava-tui/src/widgets/provider_connect.rs` | 556 | Provider connection wizard — multi-step UI. Could split steps into sub-modules |
| `ava-tools/tests/core_tools_test.rs` | 552 | Test file — large but acceptable |
| `ava-commander/tests/commander.rs` | 537 | Test file — acceptable |
| `ava-lsp/src/client.rs` | 482 | Single concern (LSP protocol). Many similar request methods — DRY with macro, but structurally fine |
| `ava-commander/src/lib.rs` | 475 | Commander + types + builder. Could extract `types.rs` for Domain/Budget/Lead |
| `ava-tui/src/headless.rs` | 474 | Headless mode runner — single concern, acceptable |
| `ava-mcp/src/transport.rs` | 472 | JSON-RPC types + transport trait. Single concern, acceptable |
| `ava-permissions/src/classifier/mod.rs` | 456 | Permission classification — review for split opportunities |
| `ava-config/src/lib.rs` | 452 | Config types + ConfigManager. Split types to `types.rs` |
| `ava-config/src/credentials.rs` | 427 | Credential store — single concern, acceptable |
| `ava-tools/src/core/custom_tool.rs` | 425 | Custom tool loading from TOML — single concern |
| `ava-tui/src/app/commands.rs` | 411 | Command handlers — could split by category |
| `ava-types/src/error.rs` | 400 | AvaError enum (~25 variants) — large but standard for central error type |
| `ava-mcp/src/client.rs` | 399 | MCP client — single concern |
| `ava-auth/src/lib.rs` | 397 | Auth facade — well-structured, delegates to submodules |
| `ava-agent/src/stuck.rs` | 383 | Stuck detection — single concern |
| `ava-llm/src/providers/common/mod.rs` | 381 | Shared provider utilities — acceptable |
| `ava-tui/tests/e2e_headless.rs` | 375 | Test file — acceptable |
| `ava-llm/src/providers/openai.rs` | 364 | OpenAI provider — single concern |
| `ava-llm/src/providers/anthropic.rs` | 364 | Anthropic provider — single concern |
| `ava-context/src/strategies/summarization.rs` | 357 | Summarization strategy — acceptable |
| `ava-mcp/src/manager.rs` | 352 | MCP server manager — acceptable |
| `ava-tools/src/monitor.rs` | 341 | Tool monitor — acceptable |
| `ava-tools/src/core/apply_patch.rs` | 336 | Patch application — acceptable |
| `ava-config/src/credential_commands.rs` | 326 | CLI credential commands — acceptable |
| `ava-tui/src/audio.rs` | 325 | Audio recording — acceptable |
| `ava-memory/src/lib.rs` | 320 | Memory system — acceptable |
| `ava-mcp/tests/mcp.rs` | 317 | Test file — acceptable |
| `ava-context/src/condenser.rs` | 315 | Context condensation — acceptable |
| `ava-mcp/src/config.rs` | 313 | MCP config parsing — acceptable |
| `ava-tui/src/app/event_handler.rs` | 308 | Event handler — acceptable |
| `ava-tui/src/ui/mod.rs` | 307 | UI rendering — acceptable |

---

## 2. God Structs (>10 fields)

### `Theme` — 28 fields (`ava-tui/src/state/theme.rs:4`)

**Problem:** Every color in the TUI theme is a top-level field.

**Suggestion:** Group into sub-structs:
```rust
struct Theme {
    chrome: ChromeColors,    // bg, fg, border, header, status_bar
    messages: MessageColors,  // user, assistant, system, tool, error
    syntax: SyntaxColors,     // keyword, string, comment, number, etc.
    diff: DiffColors,         // added, removed, context
    input: InputColors,       // prompt, cursor, selection
}
```
**Effort:** Moderate — requires updating all theme access sites.

### `AppState` — 17 fields (`ava-tui/src/app/mod.rs:38`)

**Suggestion:** Decompose into:
```rust
struct AppState {
    session: SessionState,   // messages, session_id, history
    ui: UiState,             // modal, scroll, input_mode
    agent: AgentRunState,    // running, cancelled, current_tool
    config: AppConfig,       // provider, model, settings
}
```
**Effort:** Significant — `AppState` is accessed everywhere in TUI.

### `AgentStack` — 17 fields (`ava-agent/src/stack.rs:78`)

**Suggestion:** Extract configuration from runtime services:
```rust
struct AgentStack {
    services: AgentServices,  // router, tools, session, memory, mcp
    config: StackConfig,      // max_turns, yolo, thinking_level, overrides
    platform: Arc<Platform>,
    codebase: Option<CodebaseIndex>,
}
```
**Effort:** Moderate — mostly internal to ava-agent.

### `AgentState` — 15 fields (`ava-tui/src/state/agent.rs:35`)

**Suggestion:** Similar to AppState — group into run state + display state.
**Effort:** Moderate.

### `CLIAgentConfig` — 14 fields (`ava-cli-providers/src/config.rs:6`)

**Assessment:** This is a config/builder struct. 14 fields for CLI arg parsing is
borderline acceptable. Consider using builder pattern instead.
**Effort:** Trivial.

### `CliArgs` — 14 fields (`ava-tui/src/config/cli.rs:6`)

**Assessment:** CLI argument struct (clap derive). 14 fields is standard for a
full-featured CLI. **No action needed** — clap structs are inherently flat.

---

## 3. Cross-Crate Dependency Map

```
Leaf crates (0 ava-* deps):
  ava-types, ava-auth, ava-memory, ava-permissions, ava-sandbox,
  ava-extensions, ava-lsp, ava-validator

Single-dep crates:
  ava-logger       → ava-types
  ava-platform     → ava-types
  ava-context      → ava-types
  ava-session      → ava-types
  ava-db           → ava-types

Two-dep crates:
  ava-config       → ava-auth, ava-types
  ava-llm          → ava-config, ava-types
  ava-cli-providers→ ava-llm, ava-types
  ava-codebase     → (no ava deps — leaf despite code references)
  ava-mcp          → ava-tools, ava-types

Heavy-dep crates:
  ava-tools (7)    → ava-codebase, ava-memory, ava-permissions,
                     ava-platform, ava-sandbox, ava-session, ava-types

  ava-agent (10)   → ava-codebase, ava-config, ava-context, ava-llm,
                     ava-mcp, ava-memory, ava-platform, ava-session,
                     ava-tools, ava-types

  ava-commander(7) → ava-agent, ava-cli-providers, ava-context,
                     ava-llm, ava-platform, ava-tools, ava-types

  ava-tui (10)     → ava-agent, ava-auth, ava-commander, ava-config,
                     ava-llm, ava-permissions, ava-platform,
                     ava-session, ava-tools, ava-types
```

### Dependency DAG (top → bottom = more deps)

```
                    ava-tui (binary)
                   /       |        \
          ava-commander  ava-agent  ava-config
           /    |           |    \       \
    ava-cli-providers  ava-tools  ava-llm  ava-auth
           |        /  |   |  \       \
        ava-llm   /  ava-*  \  ava-codebase  ava-types
                 /  (leaves)  \
          ava-types        ava-types
```

---

## 4. Circular Dependencies

**None found.** ✅

Initial analysis flagged `ava-tools ↔ ava-codebase` as potentially circular
because `ava-tools` imports `ava-codebase` and some `ava-codebase` source files
reference tool types. However, Cargo.toml verification confirmed:

- `ava-tools/Cargo.toml` → depends on `ava-codebase` ✅
- `ava-codebase/Cargo.toml` → has **zero** ava-* dependencies ✅

The dependency graph is a clean DAG.

---

## 5. Module Organization

### Flat directories with 8+ files

| Directory | Files | Assessment |
|-----------|-------|------------|
| `ava-tools/src/core/` | 18 | ⚠️ All 18 tool implementations in one flat directory. Consider grouping: `core/file/` (read, write, edit, glob, grep), `core/search/` (codebase_search), `core/vcs/` (git_read), `core/memory/`, `core/session/` |
| `ava-tui/src/widgets/` | 16 | Borderline — each widget is self-contained. Acceptable as-is |
| `ava-tui/src/state/` | 9 | Acceptable |
| `ava-codebase/src/` | 8 | Acceptable — well-named modules (graph, indexer, pagerank, repomap, search, types) |
| `ava-tui/src/` | 8 | Acceptable — top-level modules (app, audio, auth, config, event, headless, state, widgets) |

---

## 6. Priority Actions

### High priority (do first)

1. **Split `model_catalog.rs`** (1,075 → 4 files) — trivial effort, largest file
2. **Split `modals.rs`** (713 → 7 files) — trivial effort, mechanical extraction
3. **DRY `agent_loop/mod.rs`** `run()` vs `run_streaming()` — significant but
   eliminates ~200 lines of duplication

### Medium priority

4. **Decompose `AgentStack`** — extract MCP + tools into sub-structs
5. **Split `workflow.rs`** — extract presets and prompts
6. **Split `review.rs`** — extract types, diff, and prompt modules
7. **Group `ava-tools/src/core/`** into sub-directories

### Low priority

8. **Decompose `Theme`** into sub-structs (28 fields → 5 groups)
9. **Extract types from `ava-commander/src/lib.rs`** (Domain, Budget, Lead)
10. **Extract config types from `ava-config/src/lib.rs`**

---

## Appendix: File Size Distribution

```
0–100 lines:    ~90 files  (39%)
100–200 lines:  ~65 files  (28%)
200–300 lines:  ~44 files  (19%)
300–500 lines:  ~22 files  (10%)
500+ lines:       8 files  ( 4%)
```

The 300-line guideline from AGENTS.md is violated by ~30 files (13%).
Most violations are mild (300–400 lines) and involve test code or naturally
cohesive modules. The 8 files over 500 lines are the real targets.
