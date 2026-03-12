//! AVA MCP — Model Context Protocol client and server implementation.
//!
//! This crate provides:
//! - MCP client for connecting to MCP servers
//! - Local MCP server implementation
//! - Transport layer (stdio, HTTP, in-memory)

pub mod client;
pub mod config;
pub mod manager;
pub mod server;
pub mod transport;

pub use client::{MCPClient, MCPTool, ServerCapabilities};
pub use config::{load_mcp_config, load_merged_mcp_config, MCPServerConfig, TransportType};
pub use manager::ExtensionManager;
pub use server::AVAMCPServer;
pub use transport::{
    decode_message, encode_message, FramedTransport, HttpTransport, InMemoryTransport,
    JsonRpcError, JsonRpcMessage, MCPTransport, StdioTransport,
};

