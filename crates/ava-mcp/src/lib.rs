pub mod client;
pub mod server;
pub mod transport;

pub use client::{MCPClient, MCPServer, ServerConfig};
pub use server::AVAMCPServer;
pub use transport::{decode_message, encode_message, MCPTransport};

pub fn healthcheck() -> bool {
    true
}
