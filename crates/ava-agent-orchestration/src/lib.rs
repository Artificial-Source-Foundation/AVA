//! Orchestration-heavy agent composition seam.
//!
//! This crate owns `stack/` and `subagents/` runtime composition modules while
//! reusing runtime-core behavior from `ava-agent` through direct crate imports.

pub mod stack;
pub mod subagents;
