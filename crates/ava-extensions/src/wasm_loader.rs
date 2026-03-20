use std::path::Path;

use crate::manager::{Extension, ExtensionError};

/// WASM extension loader.
///
/// **Not yet implemented.** `WasmLoader::load` always returns
/// [`ExtensionError::Unsupported`]. WASM extension support is tracked in the
/// backlog (requires `wasmtime` integration). Do not call `load` in production
/// code paths — check `ExtensionError::Unsupported` and handle gracefully.
///
/// This type is intentionally kept internal to signal that callers should not
/// depend on WASM loading being available. Use native extensions
/// ([`crate::native_loader::load_native_extension`]) for production use.
// Kept for future wasmtime integration; used only in tests until then.
#[allow(dead_code)]
pub(crate) struct WasmLoader;

impl WasmLoader {
    #[allow(dead_code)]
    pub(crate) fn new() -> Self {
        Self
    }

    /// Attempt to load a WASM extension from `path`.
    ///
    /// Always returns [`ExtensionError::Unsupported`] — WASM extension loading
    /// is not yet implemented. The backlog tracks integration with `wasmtime`.
    #[allow(dead_code)]
    pub(crate) fn load(&self, _path: &Path) -> Result<Box<dyn Extension>, ExtensionError> {
        Err(ExtensionError::Unsupported(
            "WASM extensions are not yet implemented. \
             Use native extensions or MCP servers instead. \
             See backlog for wasmtime integration tracking."
                .to_string(),
        ))
    }
}

impl Default for WasmLoader {
    fn default() -> Self {
        Self::new()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn load_returns_unsupported() {
        let loader = WasmLoader::new();
        let result = loader.load(Path::new("/any/path.wasm"));
        assert!(
            matches!(result, Err(ExtensionError::Unsupported(_))),
            "WasmLoader::load must return Unsupported until wasmtime is integrated"
        );
    }

    #[test]
    fn load_error_message_is_actionable() {
        let loader = WasmLoader::default();
        let result = loader.load(Path::new("/any/path.wasm"));
        let err = match result {
            Err(e) => e,
            Ok(_) => panic!("expected Err, got Ok"),
        };
        let msg = err.to_string();
        // The error message should guide the caller toward alternatives.
        assert!(
            msg.contains("not yet implemented") || msg.contains("native"),
            "Error message should mention alternatives: {msg}"
        );
    }
}
