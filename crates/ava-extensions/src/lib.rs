#![allow(unsafe_code)]
//! AVA Extensions — hook primitives and extension lifecycle management.
//!
//! This crate provides:
//! - Hook registration and invocation system
//! - Extension descriptors and manager
//! - Native (shared library) and WASM extension loaders
//!
//! This surface is distinct from MCP:
//! - `ava-mcp` manages external MCP servers and MCP tool discovery
//! - `ava-extensions` manages native/WASM extension descriptors and hook registration
//!
//! In the current 3.3 baseline, this crate is primarily used by the desktop
//! extension-registration command surface and should not be conflated with MCP.

/// Hook primitives and registration for extension lifecycle integration.
pub mod hook;
/// HTTP webhook hooks for external integrations.
pub mod http_hooks;
/// Extension descriptors and registration manager.
pub mod manager;
/// Native extension shared-library loader.
pub mod native_loader;
/// WASM extension loader — not yet implemented; kept internal.
///
/// WASM support requires `wasmtime` integration (see backlog). This module is
/// private so callers cannot accidentally depend on a permanently-stubbed API.
pub(crate) mod wasm_loader;

/// Hook registration and invocation types.
pub use hook::{Hook, HookContext, HookPoint, HookRegistry};
pub use http_hooks::{fire_http_hook, HttpHook, HttpHookConfig, HttpHookError};
/// Extension descriptors, manager, and error surface.
pub use manager::{
    Extension, ExtensionDescriptor, ExtensionError, ExtensionManager, NativeExtensionDescriptor,
    WasmExtensionDescriptor,
};
pub use native_loader::load_native_extension;
