//! Agent transport trait — the core abstraction for communicating with agents.
//!
//! All agent adapters (Claude SDK, legacy CLI, ACP servers) implement this trait,
//! providing a uniform interface for query, interrupt, and cancel operations.

use std::pin::Pin;

use async_trait::async_trait;
use ava_types::Result;
use futures::Stream;

use crate::protocol::{AgentMessage, AgentQuery};

/// A stream of agent messages.
pub type AgentMessageStream = Pin<Box<dyn Stream<Item = AgentMessage> + Send>>;

/// Transport for communicating with an external agent.
///
/// Implementations handle subprocess lifecycle, protocol translation, and
/// streaming. The transport is the boundary between AVA and external agents.
#[async_trait]
pub trait AgentTransport: Send + Sync {
    /// Send a query and receive a stream of messages.
    ///
    /// The stream emits `AgentMessage` events as the agent works, ending with
    /// either a `Result` or `Error` message.
    async fn query(&self, query: AgentQuery) -> Result<AgentMessageStream>;

    /// Interrupt the running agent with a new message.
    ///
    /// The agent should stop its current task and process the interrupt message.
    /// Not all transports support this — unsupported transports return an error.
    async fn interrupt(&self, message: String) -> Result<()> {
        let _ = message;
        Err(ava_types::AvaError::ToolError(
            "interrupt not supported by this transport".into(),
        ))
    }

    /// Cancel the running agent.
    ///
    /// Sends a cancellation signal. For subprocess transports this kills the process.
    async fn cancel(&self) -> Result<()> {
        Err(ava_types::AvaError::ToolError(
            "cancel not supported by this transport".into(),
        ))
    }

    /// Human-readable name for this transport (e.g., "claude-code", "codex").
    fn name(&self) -> &str;
}

#[cfg(test)]
mod tests {
    use super::*;

    // Verify the trait is object-safe.
    fn _assert_object_safe(_: &dyn AgentTransport) {}
}
