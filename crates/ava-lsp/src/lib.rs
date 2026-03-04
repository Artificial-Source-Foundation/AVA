//! LSP client primitives for AVA.

pub mod client;
pub mod error;
pub mod transport;

pub use client::LspClient;
pub use error::{LspError, Result};
pub use transport::{decode_message, encode_message};
