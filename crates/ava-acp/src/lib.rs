//! Agent Client Protocol (ACP) — Agent SDK integration and transport.
//!
//! This crate provides:
//! - Protocol types for the Anthropic Agent SDK streaming format
//! - An `AgentTransport` trait for bidirectional agent communication
//! - A stdio transport for spawning agent subprocesses (NDJSON over stdin/stdout)
//! - Adapters for Claude Agent SDK, legacy CLI agents, and ACP servers
//!
//! Replaces the old `ava-cli-providers` crate with a proper protocol-based approach.

pub mod adapters;
pub mod factory;
pub mod protocol;
pub mod provider;
pub mod server;
pub mod stdio;
pub mod transport;

pub use factory::AcpProviderFactory;
pub use protocol::*;
pub use transport::*;

/// Info about a discovered external agent (for display in UI).
#[derive(Debug, Clone)]
pub struct DiscoveredAgent {
    pub name: String,
    pub binary: String,
    pub version: String,
}
