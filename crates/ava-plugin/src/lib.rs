//! AVA Plugin System — Power plugin runtime
//!
//! Provides the core infrastructure for AVA's Tier 2 "power" plugin system:
//! external process plugins communicating via JSON-RPC over stdio.
//!
//! # Modules
//!
//! - [`manifest`] — Parse `plugin.toml` manifest files
//! - [`discovery`] — Scan plugin directories for installed plugins
//! - [`hooks`] — Hook event types and dispatch routing
//! - [`runtime`] — Spawn and manage plugin child processes
//! - [`manager`] — Top-level plugin lifecycle manager

pub mod discovery;
pub mod hooks;
pub mod manager;
pub mod manifest;
pub mod runtime;

pub use discovery::{discover_plugins, DiscoveredPlugin};
pub use hooks::{HookDispatcher, HookEvent, HookRequest, HookResponse};
pub use manager::{PluginInfo, PluginManager, PluginStatus};
pub use manifest::{HookSubscriptions, PluginManifest, RuntimeConfig};
pub use runtime::PluginProcess;
