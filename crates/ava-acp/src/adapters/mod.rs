//! Agent adapters — concrete implementations of `AgentTransport`.
//!
//! Each adapter speaks a specific protocol:
//! - `claude_sdk`: Anthropic Agent SDK (stream-json with rich events)
//! - `legacy_cli`: Legacy CLI agents (basic stream-json or plain text)
//! - `config`: Declarative agent configuration

pub mod claude_sdk;
pub mod config;
pub mod legacy_cli;
