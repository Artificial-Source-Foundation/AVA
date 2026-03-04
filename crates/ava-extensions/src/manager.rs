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
}

impl Display for ExtensionError {
    fn fmt(&self, f: &mut Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::EmptyName => write!(f, "extension name cannot be empty"),
            Self::EmptyVersion => write!(f, "extension version cannot be empty"),
            Self::InvalidPath => write!(f, "extension path cannot be empty"),
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
    hooks: HookRegistry,
}

impl Default for ExtensionManager {
    fn default() -> Self {
        Self {
            descriptors: HashMap::new(),
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

        self.hooks
            .replace_extension(&descriptor.name, &descriptor.hooks);
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
