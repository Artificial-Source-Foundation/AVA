# ava-tui

> Terminal user interface for the AVA CLI — Ratatui-based interactive chat with headless/scripting modes.

## Public API

| Type/Function | Description |
|--------------|-------------|
| `App` | Main TUI application state machine and event loop |
| `AppState` | Centralized mutable state for the entire TUI |
| `App::new()` | Initialize App with CLI args, session, agent, and hooks |
| `App::run()` | Start the main TUI event loop (terminal setup → loop → cleanup) |
| `App::test_new()` | Constructor for testing without real terminal/agent |
| `run_headless()` | Execute tasks without TUI (scripting/CI mode) |
| `CliArgs` | Clap-derived CLI argument definitions |
| `Command` | Subcommands: Review, Auth, Plugin, Serve |
| `ViewMode` | Main, SubAgent, BackgroundTask, PraxisTask display modes |
| `ModalType` | All modal variants (CommandPalette, SessionList, ToolApproval, etc.) |
| `MessageState` | Chat message history with scroll and spinner state |
| `InputState` | Composer input buffer with autocomplete support |
| `AgentState` | Agent runtime state (mode, activity, sub-agents, cost tracking) |
| `SessionState` | SQLite-backed session persistence |
| `Theme` | Color scheme definitions for UI rendering |
| `TokenBuffer` | Adaptive token buffering to reduce flicker during streaming |
| `render()` | Main UI render function (ui/mod.rs) |

## Module Map

| File | Purpose |
|------|---------|
| `lib.rs` | Exports all public modules; feature-gated voice/benchmark/web modules |
| `main.rs` | Binary entrypoint: CLI parsing, subcommand routing, logging init |
| `app/mod.rs` | Core App struct and AppState definition (1000+ lines) |
| `app/actions.rs` | Action handlers for keybindings and commands |
| `app/commands.rs` | Slash command implementations (/help, /session, /git, etc.) |
| `app/event_handler.rs` | Event dispatch and key handling logic |
| `app/event_dispatch.rs` | Event routing to appropriate handlers |
| `app/input_handling.rs` | Input processing, autocomplete, slash menu |
| `app/spawners.rs` | Agent task spawners for foreground/background runs |
| `app/praxis.rs` | Multi-agent Praxis mode integration |
| `app/background.rs` | Background task management and worktree isolation |
| `app/modals.rs` | Modal state management and open/close logic |
| `app/git_commit.rs` | Git commit message generation UI flow |
| `app/exporting.rs` | Session export functionality |
| `app/command_support.rs` | Command palette and completion logic |
| `state/mod.rs` | State submodule exports |
| `state/agent.rs` | Agent runtime state (provider, model, cost, tokens, sub-agents) |
| `state/messages.rs` | UI message list with scroll and streaming state |
| `state/input.rs` | Input buffer with cursor, history, and autocomplete |
| `state/session.rs` | Session persistence to SQLite |
| `state/permission.rs` | Tool approval levels and permission queue |
| `state/background.rs` | Background task queue and progress tracking |
| `state/praxis.rs` | Multi-agent Praxis orchestration state |
| `state/rewind.rs` | Checkpoint/rewind state for message history |
| `state/theme.rs` | Theme definitions (Default, Dark, Light, HighContrast) |
| `state/keybinds.rs` | Keybinding definitions and action mapping |
| `state/btw.rs` | Side-conversation (BTW) overlay state |
| `state/custom_commands.rs` | TOML-defined custom slash commands registry |
| `state/voice.rs` | Voice input state (auto-submit, continuous) |
| `state/file_scanner.rs` | Async file watching and indexing |
| `ui/mod.rs` | Main render function, modal rendering, toasts |
| `ui/layout.rs` | Terminal area layout calculations |
| `ui/sidebar.rs` | Right sidebar (todos, context, info) |
| `ui/status_bar.rs` | Top bar and context bar rendering |
| `widgets/mod.rs` | Widget submodule exports |
| `widgets/message_list.rs` | Scrollable chat history with markdown rendering |
| `widgets/composer.rs` | Input text area with syntax highlighting hints |
| `widgets/command_palette.rs` | Command palette state and filtering |
| `widgets/select_list.rs` | Generic searchable list (used by SessionList, ModelSelector, etc.) |
| `widgets/tool_approval.rs` | Bottom dock for tool permission prompts |
| `widgets/diff_preview.rs` | Per-hunk diff accept/reject modal |
| `widgets/provider_connect.rs` | OAuth/API key provider setup wizard |
| `widgets/rewind_modal.rs` | Checkpoint restore UI |
| `widgets/task_list_modal.rs` | Background task status overlay |
| `widgets/slash_menu.rs` | Inline slash command autocomplete |
| `widgets/mention_picker.rs` | @-mention file/symbol picker |
| `widgets/message.rs` | Individual message bubble rendering |
| `widgets/streaming_text.rs` | Animated streaming text with spinner |
| `widgets/token_buffer.rs` | Token aggregation before display flush |
| `config/cli.rs` | CLI argument definitions with clap |
| `config/keybindings.rs` | Keybinding configuration file loading |
| `config/themes.rs` | Theme configuration and custom themes |
| `headless/mod.rs` | Headless mode entry point and subcommand routing |
| `headless/single.rs` | Single-agent headless execution |
| `headless/multi_agent.rs` | Multi-agent Praxis headless mode |
| `headless/workflow.rs` | Workflow pipeline execution (plan-code-review, etc.) |
| `headless/watch.rs` | File watcher mode for `ava:` comment triggers |
| `headless/input.rs` | Stdin message parsing (steering, follow-up, post-complete) |
| `hooks/mod.rs` | Lifecycle hooks registry |
| `hooks/runner.rs` | Hook execution with timeout and error handling |
| `hooks/events.rs` | Hook event types (PreGenerate, PostTool, etc.) |
| `hooks/config.rs` | Hook configuration loading from .ava/hooks/ |
| `rendering/markdown.rs` | Markdown parsing and styling for chat |
| `rendering/syntax.rs` | Syntax highlighting with syntect |
| `rendering/diff.rs` | Diff rendering for code changes |
| `auth.rs` | OAuth and API key authentication flows |
| `review.rs` | Code review subcommand implementation |
| `plugin_commands.rs` | Power plugin management commands |
| `event.rs` | Crossterm event reading and tick timer |
| `text_utils.rs` | Unicode-aware text truncation and display width |
| `benchmark*.rs` | Benchmark suite (feature-gated) |
| `web/*.rs` | Web server API and WebSocket (feature-gated) |

## Dependencies

Uses: ava-agent, ava-praxis, ava-llm, ava-tools, ava-session, ava-memory, ava-permissions, ava-config, ava-auth, ava-platform, ava-context, ava-types, ava-db, ava-codebase, ava-plugin

Used by: (none — binary crate)

## Key Patterns

- **Error handling**: Uses `color-eyre` for error reporting with context; panic hook restores terminal before crash
- **State management**: Centralized `AppState` with interior mutability via tokio channels for async operations
- **Token buffering**: `TokenBuffer` accumulates tokens and flushes on timer to prevent UI flicker during streaming
- **Modal system**: Enum-based modal types with unified select list widget; ToolApproval renders inline as dock
- **Event loop**: Single-threaded with tokio::select! over crossterm events, question receiver, and approval receiver
- **Theme system**: Comprehensive color palette (bg_deep, bg, bg_elevated, accent, text, text_muted, border, success, warning, error)
- **Run routing**: `run_id` allocation routes agent events to foreground, background, or praxis views
- **Feature flags**: `web` (axum server), `voice` (cpal/whisper), `benchmark` (harnessed benchmarks), `local-whisper`
- **Logging**: Daily-rotated file logs with non-blocking writer; stderr only in headless mode (TUI corrupts stderr)
- **Headless modes**: Single agent, multi-agent Praxis, workflow pipelines, file watch mode, voice loop
