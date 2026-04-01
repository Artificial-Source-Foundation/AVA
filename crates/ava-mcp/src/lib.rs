//! AVA MCP — Model Context Protocol client and server implementation.
//!
//! This crate provides:
//! - MCP client for connecting to MCP servers
//! - Local MCP server implementation
//! - Transport layer (stdio, HTTP/SSE, in-memory)
//! - OAuth 2.0 PKCE flow for remote MCP servers
//! - OAuth discovery for enterprise MCP servers
//! - Prompt templates as commands
//! - Connection health tracking
//! - Output validation and binary blob handling

pub mod auth;
pub mod client;
pub mod config;
pub mod manager;
pub mod oauth;
pub mod prompts;
pub mod server;
pub mod transport;

pub use auth::{
    discover_oauth, exchange_code, is_expired, load_mcp_tokens, refresh_token as refresh_mcp_token,
    save_mcp_tokens, start_auth_flow, AuthFlowState, McpOAuthProvider, OAuthMetadata, TokenSet,
};
pub use client::{
    ConnectionHealth, MCPClient, MCPPrompt, MCPPromptArgument, MCPPromptContent, MCPPromptMessage,
    MCPPromptResult, MCPResource, MCPResourceContent, MCPTool, ServerCapabilities,
};
pub use config::{
    load_mcp_config, load_merged_mcp_config, MCPServerConfig, McpOAuthConfig, TransportType,
};
pub use manager::{ExtensionManager, MAX_MCP_OUTPUT_CHARS};
pub use oauth::{load_stored_tokens, store_tokens, McpOAuthManager, McpTokens};
pub use prompts::{execute_mcp_prompt, get_mcp_prompt_commands, McpPromptArg, McpPromptCommand};
pub use server::AVAMCPServer;
pub use transport::{
    decode_message, encode_message, FramedTransport, HttpTransport, HttpTransportConfig,
    InMemoryTransport, JsonRpcError, JsonRpcMessage, MCPTransport, StdioTransport,
};
