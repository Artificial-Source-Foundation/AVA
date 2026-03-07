pub mod client;
pub mod config;
pub mod manager;
pub mod server;
pub mod transport;

pub use client::{MCPClient, MCPTool, ServerCapabilities};
pub use config::{load_mcp_config, MCPServerConfig, TransportType};
pub use manager::ExtensionManager;
pub use server::AVAMCPServer;
pub use transport::{
    decode_message, encode_message, FramedTransport, HttpTransport, InMemoryTransport,
    JsonRpcError, JsonRpcMessage, MCPTransport, StdioTransport,
};

pub fn healthcheck() -> bool {
    true
}
