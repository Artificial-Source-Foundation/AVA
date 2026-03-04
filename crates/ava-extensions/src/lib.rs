/// Hook primitives and registration for extension lifecycle integration.
pub mod hook;
/// Extension descriptors and registration manager.
pub mod manager;

/// Hook registration and invocation types.
pub use hook::{Hook, HookContext, HookPoint, HookRegistry};
/// Extension descriptors, manager, and error surface.
pub use manager::{
    Extension, ExtensionDescriptor, ExtensionError, ExtensionManager, NativeExtensionDescriptor,
    WasmExtensionDescriptor,
};
