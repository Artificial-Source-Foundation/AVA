use std::path::Path;

use crate::manager::{Extension, ExtensionError};

pub struct WasmLoader;

impl WasmLoader {
    pub fn new() -> Self {
        Self
    }

    pub fn load(&self, _path: &Path) -> Result<Box<dyn Extension>, ExtensionError> {
        // TODO: Integrate with wasmtime to support loading WASM extensions.
        Err(ExtensionError::Unsupported(
            "WASM extensions not yet supported".to_string(),
        ))
    }
}

impl Default for WasmLoader {
    fn default() -> Self {
        Self::new()
    }
}
