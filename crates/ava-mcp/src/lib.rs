//! AVA MCP — Model Context Protocol client and server implementation.
//!
//! This crate provides:
//! - MCP client for connecting to MCP servers
//! - Local MCP server implementation
//! - Transport layer (stdio, HTTP/SSE, in-memory)
//! - OAuth 2.0 PKCE flow for remote MCP servers

pub mod client;
pub mod config;
pub mod manager;
pub mod oauth;
pub mod server;
pub mod transport;

pub use client::{
    MCPClient, MCPPrompt, MCPPromptArgument, MCPPromptContent, MCPPromptMessage, MCPPromptResult,
    MCPResource, MCPResourceContent, MCPTool, ServerCapabilities,
};
pub use config::{
    load_mcp_config, load_merged_mcp_config, MCPServerConfig, McpOAuthConfig, TransportType,
};
pub use manager::ExtensionManager;
pub use oauth::{load_stored_tokens, store_tokens, McpOAuthManager, McpTokens};
pub use server::AVAMCPServer;
pub use transport::{
    decode_message, encode_message, FramedTransport, HttpTransport, HttpTransportConfig,
    InMemoryTransport, JsonRpcError, JsonRpcMessage, MCPTransport, StdioTransport,
};
