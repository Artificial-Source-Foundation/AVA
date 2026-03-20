#![allow(unsafe_code)]
//! AVA Extensions — hook primitives and extension lifecycle management.
//!
//! This crate provides:
//! - Hook registration and invocation system
//! - Extension descriptors and manager
//! - Native (shared library) and WASM extension loaders

/// Hook primitives and registration for extension lifecycle integration.
pub mod hook;
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
/// Extension descriptors, manager, and error surface.
pub use manager::{
    Extension, ExtensionDescriptor, ExtensionError, ExtensionManager, NativeExtensionDescriptor,
    WasmExtensionDescriptor,
};
pub use native_loader::load_native_extension;
