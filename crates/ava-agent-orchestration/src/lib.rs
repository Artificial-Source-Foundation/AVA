//! Orchestration-heavy agent composition seam.
//!
//! This crate owns `stack/` and `subagents/` runtime composition modules while
//! reusing runtime-core behavior from `ava-agent`.

// Re-export runtime-core modules expected by the extracted stack code.
pub use ava_agent::{
    agent_loop, budget, instruction_resolver, instructions, memory_enrichment, message_queue,
    routing, session_logger, system_prompt,
};

pub mod stack;
pub mod subagents;
