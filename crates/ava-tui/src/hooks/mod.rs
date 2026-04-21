//! Hook system for AVA — user-defined lifecycle automation.
//!
//! Hooks are TOML-defined actions that execute at specific lifecycle events:
//! tool calls, session start/end, agent completion, model switches, and more.
//!
//! Hook files are loaded from `.ava/hooks/*.toml` (project) and
//! `$XDG_CONFIG_HOME/ava/hooks/*.toml` (global). Project hooks take precedence.
//!
//! ## Hook types
//!
//! - **Command** — Run a shell command. Exit code 0 = success, 2 = block.
//! - **HTTP** — POST event data as JSON to a URL.
//! - **Prompt** — LLM yes/no decision (stub, requires LLM access).
//!
//! ## Example hook file
//!
//! ```toml
//! event = "PostToolUse"
//! description = "Auto-format code after edits"
//! matcher = "edit|write|multiedit|apply_patch"
//! path_pattern = "*.rs"
//! priority = 50
//! enabled = true
//!
//! [action]
//! type = "command"
//! command = "cargo fmt"
//! timeout = 10
//! ```

pub mod config;
pub mod events;
pub mod runner;

pub use config::{HookAction, HookConfig, HookRegistry, HookSource};
pub use events::{HookContext, HookEvent};
pub use runner::{HookExecution, HookResult, HookRunner};
