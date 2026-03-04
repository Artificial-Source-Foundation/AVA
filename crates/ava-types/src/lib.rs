//! AVA Core Types
//!
//! Provides the fundamental types for the AVA system including:
//! - Error types and error handling
//! - Tool definitions and tool calls
//! - Messages, sessions, and context management

pub mod context;
pub mod error;
pub mod message;
pub mod session;
pub mod tool;

pub use context::Context;
pub use error::{AvaError, ErrorCategory, Result};
pub use message::{Message, Role};
pub use session::Session;
pub use tool::{Tool, ToolCall, ToolResult};

#[cfg(test)]
mod tests {
    use uuid::Uuid;

    #[test]
    fn test_uuid_generation() {
        let uuid1 = Uuid::new_v4();
        let uuid2 = Uuid::new_v4();
        assert_ne!(uuid1, uuid2);
    }
}
