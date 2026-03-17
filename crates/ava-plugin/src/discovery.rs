//! Plugin discovery — scan directories for installed plugins.

use crate::manifest::{load_manifest, PluginManifest};
use std::path::PathBuf;
use tracing::{debug, warn};

/// A plugin found on disk with its parsed manifest.
#[derive(Debug, Clone)]
pub struct DiscoveredPlugin {
    /// Path to the plugin directory (containing `plugin.toml`).
    pub path: PathBuf,
    /// Parsed manifest from `plugin.toml`.
    pub manifest: PluginManifest,
}

/// Scan the given directories for subdirectories containing a `plugin.toml`.
///
/// Each entry in `dirs` is expected to be a plugin root directory (e.g.
/// `~/.ava/plugins/` or `.ava/plugins/`). Each immediate subdirectory
/// that contains a `plugin.toml` is treated as a discovered plugin.
///
/// Directories that don't exist or can't be read are silently skipped.
/// Subdirectories with missing or invalid manifests emit a warning and are skipped.
pub fn discover_plugins(dirs: &[PathBuf]) -> Vec<DiscoveredPlugin> {
    let mut plugins = Vec::new();

    for dir in dirs {
        if !dir.is_dir() {
            debug!("plugin directory does not exist: {}", dir.display());
            continue;
        }

        let entries = match std::fs::read_dir(dir) {
            Ok(entries) => entries,
            Err(e) => {
                warn!("failed to read plugin directory {}: {e}", dir.display());
                continue;
            }
        };

        for entry in entries {
            let entry = match entry {
                Ok(e) => e,
                Err(e) => {
                    warn!("failed to read directory entry in {}: {e}", dir.display());
                    continue;
                }
            };

            let plugin_dir = entry.path();
            if !plugin_dir.is_dir() {
                continue;
            }

            let manifest_path = plugin_dir.join("plugin.toml");
            if !manifest_path.exists() {
                debug!("skipping {} — no plugin.toml found", plugin_dir.display());
                continue;
            }

            match load_manifest(&manifest_path) {
                Ok(manifest) => {
                    debug!(
                        "discovered plugin: {} v{} at {}",
                        manifest.plugin.name,
                        manifest.plugin.version,
                        plugin_dir.display()
                    );
                    plugins.push(DiscoveredPlugin {
                        path: plugin_dir,
                        manifest,
                    });
                }
                Err(e) => {
                    warn!(
                        "skipping plugin at {} — invalid manifest: {e}",
                        plugin_dir.display()
                    );
                }
            }
        }
    }

    plugins
}

/// Returns the default plugin directories to scan.
///
/// - `~/.ava/plugins/` (global)
/// - `.ava/plugins/` (project-local, relative to cwd)
pub fn default_plugin_dirs() -> Vec<PathBuf> {
    let mut dirs = Vec::new();

    if let Some(home) = dirs::home_dir() {
        dirs.push(home.join(".ava").join("plugins"));
    }

    // Project-local
    dirs.push(PathBuf::from(".ava/plugins"));

    dirs
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::fs;
    use std::io::Write;
    use std::path::Path;

    fn write_manifest(dir: &Path, name: &str) {
        let plugin_dir = dir.join(name);
        fs::create_dir_all(&plugin_dir).unwrap();
        let manifest_path = plugin_dir.join("plugin.toml");
        let mut f = fs::File::create(manifest_path).unwrap();
        write!(
            f,
            r#"
[plugin]
name = "{name}"
version = "0.1.0"

[runtime]
command = "echo"
args = ["hello"]

[hooks]
subscribe = ["auth"]
"#
        )
        .unwrap();
    }

    #[test]
    fn discover_in_temp_dir() {
        let tmp = tempfile::TempDir::new().unwrap();
        write_manifest(tmp.path(), "plugin-a");
        write_manifest(tmp.path(), "plugin-b");

        let plugins = discover_plugins(&[tmp.path().to_path_buf()]);
        assert_eq!(plugins.len(), 2);

        let names: Vec<&str> = plugins
            .iter()
            .map(|p| p.manifest.plugin.name.as_str())
            .collect();
        assert!(names.contains(&"plugin-a"));
        assert!(names.contains(&"plugin-b"));
    }

    #[test]
    fn skip_dirs_without_manifest() {
        let tmp = tempfile::TempDir::new().unwrap();

        // A proper plugin
        write_manifest(tmp.path(), "good-plugin");

        // A directory without plugin.toml
        fs::create_dir_all(tmp.path().join("no-manifest")).unwrap();

        // A file (not a directory)
        fs::write(tmp.path().join("not-a-dir.txt"), "hello").unwrap();

        let plugins = discover_plugins(&[tmp.path().to_path_buf()]);
        assert_eq!(plugins.len(), 1);
        assert_eq!(plugins[0].manifest.plugin.name, "good-plugin");
    }

    #[test]
    fn nonexistent_directory() {
        let plugins = discover_plugins(&[PathBuf::from("/nonexistent/plugins")]);
        assert!(plugins.is_empty());
    }

    #[test]
    fn empty_directory() {
        let tmp = tempfile::TempDir::new().unwrap();
        let plugins = discover_plugins(&[tmp.path().to_path_buf()]);
        assert!(plugins.is_empty());
    }

    #[test]
    fn multiple_search_dirs() {
        let tmp1 = tempfile::TempDir::new().unwrap();
        let tmp2 = tempfile::TempDir::new().unwrap();
        write_manifest(tmp1.path(), "plugin-1");
        write_manifest(tmp2.path(), "plugin-2");

        let plugins = discover_plugins(&[tmp1.path().to_path_buf(), tmp2.path().to_path_buf()]);
        assert_eq!(plugins.len(), 2);
    }

    #[test]
    fn skip_invalid_manifest() {
        let tmp = tempfile::TempDir::new().unwrap();
        let bad_dir = tmp.path().join("bad-plugin");
        fs::create_dir_all(&bad_dir).unwrap();
        fs::write(bad_dir.join("plugin.toml"), "not valid toml {{{").unwrap();

        let plugins = discover_plugins(&[tmp.path().to_path_buf()]);
        assert!(plugins.is_empty());
    }
}
