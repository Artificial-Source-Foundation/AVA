# ava-extensions

Extension system with hook points, native shared library loading, and WASM stub.

## How It Works

### Extension Manager (`src/manager.rs`)

`ExtensionManager` registers extensions and dispatches tool calls and hooks:

```rust
pub struct ExtensionManager {
    extensions: Vec<ExtensionDescriptor>,
    hooks: HookRegistry,
}
```

Extensions are registered as either `Native` (shared library) or `Wasm` descriptors. The manager provides:

- `register_native(path)` -- loads a `.so`/`.dylib`/`.dll` extension
- `register_wasm(path)` -- stub (returns `Unsupported`)
- `call_tool(name, args)` -- dispatches to the extension that provides the tool
- `invoke_hook(point, context)` -- fires all hooks registered for a hook point

### Extension Trait

```rust
pub trait Extension: Send + Sync {
    fn name(&self) -> &str;
    fn version(&self) -> &str;
    fn tools(&self) -> Vec<Tool>;
    fn execute_tool(&self, name: &str, args: Value) -> Result<String>;
    fn hooks(&self) -> Vec<HookPoint>;
    fn on_hook(&self, point: HookPoint, context: &HookContext) -> Result<()>;
}
```

### Hooks (`src/hook.rs`)

```rust
pub enum HookPoint {
    BeforeToolExecution,
    AfterToolExecution,
}

pub struct HookContext {
    pub tool_name: String,
    pub arguments: Value,
    pub result: Option<String>,
}
```

`HookRegistry` stores hooks by `HookPoint` and invokes them in registration order.

### Native Loader (`src/native_loader.rs`)

`load_native_extension(path)` uses `libloading` to load a shared library and looks for the `ava_extension_create` symbol, which must return a `Box<dyn Extension>`.

### WASM Loader (`src/wasm_loader.rs`)

`WasmLoader` is a stub that returns `AvaError::UnsupportedError`. WASM extension support is planned but not yet implemented.

## Source Files

| File | Lines | Purpose |
|------|------:|---------|
| `src/lib.rs` | -- | Re-exports |
| `src/manager.rs` | -- | ExtensionManager, ExtensionDescriptor |
| `src/hook.rs` | -- | HookPoint, HookContext, HookRegistry |
| `src/native_loader.rs` | -- | Shared library loading via libloading |
| `src/wasm_loader.rs` | -- | WASM stub (returns Unsupported) |
