use std::collections::HashMap;
use std::fmt::{Display, Formatter};
use std::path::PathBuf;

use crate::hook::{Hook, HookContext, HookPoint, HookRegistry};

/// Validation and registration errors for extension descriptors.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum ExtensionError {
    /// Extension name is empty or whitespace.
    EmptyName,
    /// Extension version is empty or whitespace.
    EmptyVersion,
    /// Extension path is empty.
    InvalidPath,
    /// Extension binary does not exist on disk.
    FileNotFound(PathBuf),
    /// Extension binary could not be loaded.
    LoadFailure(String),
    /// Required symbol was not found in extension binary.
    MissingSymbol(String),
    /// Feature is intentionally unimplemented.
    Unsupported(String),
}

impl Display for ExtensionError {
    fn fmt(&self, f: &mut Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::EmptyName => write!(f, "extension name cannot be empty"),
            Self::EmptyVersion => write!(f, "extension version cannot be empty"),
            Self::InvalidPath => write!(f, "extension path cannot be empty"),
            Self::FileNotFound(path) => {
                write!(f, "extension file not found: {}", path.display())
            }
            Self::LoadFailure(message) => write!(f, "extension load failure: {message}"),
            Self::MissingSymbol(symbol) => {
                write!(f, "extension missing required symbol: {symbol}")
            }
            Self::Unsupported(message) => write!(f, "{message}"),
        }
    }
}

impl std::error::Error for ExtensionError {}

/// Descriptor for native extensions linked into the host process.
#[derive(Clone)]
pub struct NativeExtensionDescriptor {
    /// Stable extension identifier.
    pub name: String,
    /// Semantic or internal extension version.
    pub version: String,
    /// Filesystem path associated with the extension.
    pub path: PathBuf,
    /// Tool names exposed by this extension.
    pub tools: Vec<String>,
    /// Hook handlers registered by this extension.
    pub hooks: Vec<Hook>,
    /// Validator identifiers provided by this extension.
    pub validators: Vec<String>,
}

/// Descriptor for WASM extensions discovered at runtime.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct WasmExtensionDescriptor {
    /// Stable extension identifier.
    pub name: String,
    /// Semantic or internal extension version.
    pub version: String,
    /// Filesystem path to the WASM module.
    pub path: PathBuf,
    /// Tool names exposed by this extension.
    pub tools: Vec<String>,
    /// Hook points advertised by this extension.
    pub hooks: Vec<HookPoint>,
    /// Validator identifiers provided by this extension.
    pub validators: Vec<String>,
    /// Free-form metadata for extension discovery and routing.
    pub metadata: HashMap<String, String>,
}

/// Union of supported extension descriptor sources.
#[derive(Clone)]
pub enum ExtensionDescriptor {
    /// Native extension descriptor.
    Native(NativeExtensionDescriptor),
    /// WASM extension descriptor.
    Wasm(WasmExtensionDescriptor),
}

/// Contract implemented by native extension types.
pub trait Extension {
    /// Returns the extension descriptor used for registration.
    fn descriptor(&self) -> NativeExtensionDescriptor;
}

/// In-memory registry for extension descriptors and hooks.
pub struct ExtensionManager {
    descriptors: HashMap<String, ExtensionDescriptor>,
    tool_dispatch: HashMap<String, String>,
    hooks: HookRegistry,
}

impl Default for ExtensionManager {
    fn default() -> Self {
        Self {
            descriptors: HashMap::new(),
            tool_dispatch: HashMap::new(),
            hooks: HookRegistry::new(),
        }
    }
}

impl ExtensionManager {
    /// Creates an empty extension manager.
    pub fn new() -> Self {
        Self::default()
    }

    /// Registers or replaces a native extension and its hooks.
    pub fn register_native<E: Extension>(&mut self, extension: E) -> Result<(), ExtensionError> {
        let descriptor = extension.descriptor();
        validate_descriptor(&descriptor.name, &descriptor.version, &descriptor.path)?;
        tracing::info!(
            "Extension registered: {} v{}",
            descriptor.name,
            descriptor.version
        );

        self.hooks
            .replace_extension(&descriptor.name, &descriptor.hooks);
        for tool_name in &descriptor.tools {
            self.register_tool(&descriptor.name, tool_name);
        }
        self.descriptors.insert(
            descriptor.name.clone(),
            ExtensionDescriptor::Native(descriptor),
        );

        Ok(())
    }

    /// Registers or replaces a WASM extension descriptor.
    pub fn register_wasm_module(
        &mut self,
        descriptor: WasmExtensionDescriptor,
    ) -> Result<(), ExtensionError> {
        validate_descriptor(&descriptor.name, &descriptor.version, &descriptor.path)?;

        self.hooks.remove_extension(&descriptor.name);
        for tool_name in &descriptor.tools {
            self.register_tool(&descriptor.name, tool_name);
        }
        self.descriptors.insert(
            descriptor.name.clone(),
            ExtensionDescriptor::Wasm(descriptor),
        );

        Ok(())
    }

    /// Returns a descriptor by extension name.
    pub fn get_descriptor(&self, name: &str) -> Option<&ExtensionDescriptor> {
        self.descriptors.get(name)
    }

    /// Invokes hooks for the specified point.
    pub fn invoke_hooks(&self, point: HookPoint, context: &HookContext) -> Vec<String> {
        self.hooks.invoke(point, context)
    }

    /// Registers a single tool ownership mapping.
    pub fn register_tool(&mut self, extension_name: &str, tool_name: &str) {
        self.tool_dispatch
            .insert(tool_name.to_string(), extension_name.to_string());
    }

    /// Resolves the extension currently owning a tool.
    pub fn get_extension_for_tool(&self, tool_name: &str) -> Option<&str> {
        self.tool_dispatch.get(tool_name).map(String::as_str)
    }
}

fn validate_descriptor(
    name: &str,
    version: &str,
    path: &std::path::Path,
) -> Result<(), ExtensionError> {
    if name.trim().is_empty() {
        return Err(ExtensionError::EmptyName);
    }
    if version.trim().is_empty() {
        return Err(ExtensionError::EmptyVersion);
    }
    if path.as_os_str().is_empty() {
        return Err(ExtensionError::InvalidPath);
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use std::path::PathBuf;

    use super::*;

    #[test]
    fn test_register_tool_dispatch_lookup() {
        let mut manager = ExtensionManager::new();
        manager.register_tool("git_ext", "git.status");

        let owner = manager
            .get_extension_for_tool("git.status")
            .expect("tool should be registered");
        assert_eq!(owner, "git_ext");
    }

    #[test]
    fn test_native_registration_populates_tool_dispatch() {
        struct DemoExtension;

        impl Extension for DemoExtension {
            fn descriptor(&self) -> NativeExtensionDescriptor {
                NativeExtensionDescriptor {
                    name: "demo".to_string(),
                    version: "1.0.0".to_string(),
                    path: PathBuf::from("demo.so"),
                    tools: vec!["demo.run".to_string()],
                    hooks: Vec::new(),
                    validators: Vec::new(),
                }
            }
        }

        let mut manager = ExtensionManager::new();
        manager
            .register_native(DemoExtension)
            .expect("registration should succeed");

        let owner = manager
            .get_extension_for_tool("demo.run")
            .expect("tool should be mapped");
        assert_eq!(owner, "demo");
    }
}
