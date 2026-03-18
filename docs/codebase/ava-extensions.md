# ava-extensions

> Extension system with hooks and native/WASM loaders

## Public API

| Type/Function | Description |
|--------------|-------------|
| `Hook` | Named callback bound to a hook point and extension |
| `HookContext` | Context passed to hook invocations with scope and metadata |
| `HookPoint` | Enum of stable extension hook points (BeforeToolExecution, AfterToolExecution) |
| `HookRegistry` | Registry grouping hooks by point for fast invocation |
| `Extension` | Trait for native extension types returning descriptors |
| `ExtensionManager` | In-memory registry for extension descriptors and hooks |
| `ExtensionDescriptor` | Union of native and WASM extension descriptors |
| `NativeExtensionDescriptor` | Descriptor for native shared-library extensions |
| `WasmExtensionDescriptor` | Descriptor for WASM module extensions |
| `ExtensionError` | Validation/registration errors (EmptyName, LoadFailure, etc.) |
| `load_native_extension()` | Unsafe function to load native Rust extensions from shared libraries |
| `WasmLoader` | WASM extension loader (currently unimplemented) |

## Module Map

| File | Purpose |
|------|---------|
| `lib.rs` | Exports hook types, manager types, and loader functions |
| `hook.rs` | Hook primitives: HookPoint, HookContext, Hook, HookRegistry |
| `manager.rs` | ExtensionManager, ExtensionDescriptor types, ExtensionError |
| `native_loader.rs` | Unsafe native shared library loading via libloading |
| `wasm_loader.rs` | WASM loader placeholder (returns Unsupported error) |

## Dependencies

Uses: (none - only external crates: libloading, tracing)

Used by: ava-agent, ava-tui

## Key Patterns

- **Hook registration**: Hooks are registered per-extension via `HookRegistry::replace_extension()`
- **Extension validation**: `validate_descriptor()` ensures non-empty name, version, and path
- **Native loading safety**: Uses `libloading` with `#[allow(unsafe_code)]` and extensive SAFETY comments
- **Library lifecycle**: `std::mem::forget(library)` to keep vtable valid; extensions cannot be unloaded
- **Tool dispatch**: ExtensionManager maintains `tool_dispatch` HashMap mapping tool names to extension names
- **Error handling**: `ExtensionError` enum with `std::error::Error` impl and `Display` for actionable messages
- **WASM unimplemented**: `WasmLoader` returns `ExtensionError::Unsupported` until wasmtime integration
