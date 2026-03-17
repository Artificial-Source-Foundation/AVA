//! Plugin manifest parsing (`plugin.toml`).

use ava_types::{AvaError, Result};
use serde::{Deserialize, Serialize};
use std::collections::HashMap;
use std::path::Path;

/// A parsed `plugin.toml` manifest describing a power plugin.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct PluginManifest {
    /// Plugin metadata.
    pub plugin: PluginMeta,
    /// How to spawn the plugin process.
    pub runtime: RuntimeConfig,
    /// Which hooks this plugin subscribes to.
    #[serde(default)]
    pub hooks: HookSubscriptions,
}

/// Plugin identity and metadata.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct PluginMeta {
    /// Unique plugin name (e.g. "copilot-auth").
    pub name: String,
    /// Semver version string.
    pub version: String,
    /// Human-readable description.
    #[serde(default)]
    pub description: String,
    /// Author name or organization.
    #[serde(default)]
    pub author: String,
}

/// Runtime configuration for spawning the plugin process.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct RuntimeConfig {
    /// The executable command (e.g. "node", "python3", "./plugin").
    pub command: String,
    /// Arguments to pass to the command.
    #[serde(default)]
    pub args: Vec<String>,
    /// Extra environment variables for the plugin process.
    #[serde(default)]
    pub env: HashMap<String, String>,
}

/// Hook subscriptions declared by the plugin.
#[derive(Debug, Clone, Default, Serialize, Deserialize)]
pub struct HookSubscriptions {
    /// List of hook event names this plugin wants to receive.
    #[serde(default)]
    pub subscribe: Vec<String>,
}

/// Load and parse a `plugin.toml` manifest from the given path.
pub fn load_manifest(path: &Path) -> Result<PluginManifest> {
    let content = std::fs::read_to_string(path).map_err(|e| {
        AvaError::ConfigError(format!(
            "failed to read plugin manifest {}: {e}",
            path.display()
        ))
    })?;
    let manifest: PluginManifest = toml::from_str(&content).map_err(|e| {
        AvaError::ConfigError(format!(
            "failed to parse plugin manifest {}: {e}",
            path.display()
        ))
    })?;
    Ok(manifest)
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::Write;

    #[test]
    fn parse_valid_manifest() {
        let toml_str = r#"
[plugin]
name = "copilot-auth"
version = "0.1.0"
description = "GitHub Copilot auth for AVA"
author = "ASF Group"

[runtime]
command = "node"
args = ["index.js"]

[hooks]
subscribe = ["auth", "request.headers", "tool.before"]
"#;
        let mut tmp = tempfile::NamedTempFile::new().unwrap();
        tmp.write_all(toml_str.as_bytes()).unwrap();
        tmp.flush().unwrap();

        let manifest = load_manifest(tmp.path()).unwrap();
        assert_eq!(manifest.plugin.name, "copilot-auth");
        assert_eq!(manifest.plugin.version, "0.1.0");
        assert_eq!(manifest.plugin.description, "GitHub Copilot auth for AVA");
        assert_eq!(manifest.plugin.author, "ASF Group");
        assert_eq!(manifest.runtime.command, "node");
        assert_eq!(manifest.runtime.args, vec!["index.js"]);
        assert_eq!(
            manifest.hooks.subscribe,
            vec!["auth", "request.headers", "tool.before"]
        );
    }

    #[test]
    fn parse_minimal_manifest() {
        let toml_str = r#"
[plugin]
name = "minimal"
version = "0.1.0"

[runtime]
command = "./plugin"
"#;
        let mut tmp = tempfile::NamedTempFile::new().unwrap();
        tmp.write_all(toml_str.as_bytes()).unwrap();
        tmp.flush().unwrap();

        let manifest = load_manifest(tmp.path()).unwrap();
        assert_eq!(manifest.plugin.name, "minimal");
        assert!(manifest.plugin.description.is_empty());
        assert!(manifest.plugin.author.is_empty());
        assert!(manifest.runtime.args.is_empty());
        assert!(manifest.hooks.subscribe.is_empty());
    }

    #[test]
    fn missing_required_fields() {
        // Missing [runtime] section
        let toml_str = r#"
[plugin]
name = "bad"
version = "0.1.0"
"#;
        let mut tmp = tempfile::NamedTempFile::new().unwrap();
        tmp.write_all(toml_str.as_bytes()).unwrap();
        tmp.flush().unwrap();

        let result = load_manifest(tmp.path());
        assert!(result.is_err());
    }

    #[test]
    fn invalid_toml() {
        let mut tmp = tempfile::NamedTempFile::new().unwrap();
        tmp.write_all(b"this is not valid toml {{{").unwrap();
        tmp.flush().unwrap();

        let result = load_manifest(tmp.path());
        assert!(result.is_err());
    }

    #[test]
    fn nonexistent_file() {
        let result = load_manifest(Path::new("/nonexistent/plugin.toml"));
        assert!(result.is_err());
    }

    #[test]
    fn manifest_with_env() {
        let toml_str = r#"
[plugin]
name = "env-plugin"
version = "0.1.0"

[runtime]
command = "python3"
args = ["main.py"]

[runtime.env]
NODE_ENV = "production"
PLUGIN_DEBUG = "1"
"#;
        let mut tmp = tempfile::NamedTempFile::new().unwrap();
        tmp.write_all(toml_str.as_bytes()).unwrap();
        tmp.flush().unwrap();

        let manifest = load_manifest(tmp.path()).unwrap();
        assert_eq!(manifest.runtime.env.get("NODE_ENV").unwrap(), "production");
        assert_eq!(manifest.runtime.env.get("PLUGIN_DEBUG").unwrap(), "1");
    }
}
