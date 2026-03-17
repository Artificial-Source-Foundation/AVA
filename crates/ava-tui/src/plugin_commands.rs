//! `ava plugin` CLI subcommand — manage power plugins.

use ava_plugin::discovery::{default_plugin_dirs, discover_plugins, DiscoveredPlugin};
use ava_plugin::manifest::load_manifest;
use color_eyre::Result;
use std::path::{Path, PathBuf};

use crate::config::cli::PluginCommand;

pub async fn run_plugin(cmd: PluginCommand) -> Result<()> {
    match cmd {
        PluginCommand::List => plugin_list(),
        PluginCommand::Add { source } => plugin_add(&source).await,
        PluginCommand::Remove { name } => plugin_remove(&name),
        PluginCommand::Info { name } => plugin_info(&name),
    }
}

/// List all discovered plugins in a table.
fn plugin_list() -> Result<()> {
    let dirs = default_plugin_dirs();
    let plugins = discover_plugins(&dirs);

    if plugins.is_empty() {
        println!("No plugins installed.");
        println!();
        println!("Install a plugin:");
        println!("  ava plugin add <path>    # from local directory");
        println!("  ava plugin add <package> # from npm package");
        return Ok(());
    }

    println!("{:<24} {:<12} {:<28} STATUS", "NAME", "VERSION", "HOOKS");
    println!("{}", "-".repeat(76));

    for plugin in &plugins {
        let m = &plugin.manifest;
        let hooks = if m.hooks.subscribe.is_empty() {
            "(none)".to_string()
        } else {
            m.hooks.subscribe.join(", ")
        };
        // All discovered plugins are on-disk and valid
        println!(
            "{:<24} {:<12} {:<28} enabled",
            m.plugin.name, m.plugin.version, hooks,
        );
    }

    println!();
    println!("{} plugin(s) installed", plugins.len());

    Ok(())
}

/// Install a plugin from a local path or npm package.
async fn plugin_add(source: &str) -> Result<()> {
    let source_path = Path::new(source);

    if source_path.is_dir() {
        // Local directory install
        install_from_local(source_path)?;
    } else if source_path.exists() {
        return Err(color_eyre::eyre::eyre!(
            "Source exists but is not a directory: {source}"
        ));
    } else {
        // Treat as npm package name
        install_from_npm(source).await?;
    }

    Ok(())
}

/// Copy a local plugin directory to `~/.ava/plugins/<name>/`.
fn install_from_local(source: &Path) -> Result<()> {
    let manifest_path = source.join("plugin.toml");
    if !manifest_path.exists() {
        return Err(color_eyre::eyre::eyre!(
            "No plugin.toml found in {}",
            source.display()
        ));
    }

    let manifest = load_manifest(&manifest_path).map_err(|e| {
        color_eyre::eyre::eyre!("Invalid plugin manifest in {}: {e}", source.display())
    })?;

    let dest = global_plugin_dir()?.join(&manifest.plugin.name);

    if dest.exists() {
        // Remove old version
        std::fs::remove_dir_all(&dest).map_err(|e| {
            color_eyre::eyre::eyre!(
                "Failed to remove existing plugin at {}: {e}",
                dest.display()
            )
        })?;
    }

    copy_dir_recursive(source, &dest)?;

    println!(
        "Installed plugin: {} v{} -> {}",
        manifest.plugin.name,
        manifest.plugin.version,
        dest.display()
    );

    Ok(())
}

/// Install a plugin from an npm package using `npm pack` + extraction.
async fn install_from_npm(package: &str) -> Result<()> {
    let tmp = tempfile::TempDir::new()?;

    // Run npm pack to download the tarball
    println!("Downloading {package} via npm...");
    let output = tokio::process::Command::new("npm")
        .args(["pack", package, "--pack-destination", "."])
        .current_dir(tmp.path())
        .output()
        .await
        .map_err(|e| color_eyre::eyre::eyre!("Failed to run npm pack: {e}"))?;

    if !output.status.success() {
        let stderr = String::from_utf8_lossy(&output.stderr);
        return Err(color_eyre::eyre::eyre!("npm pack failed: {stderr}"));
    }

    // Find the downloaded .tgz
    let tgz_name = String::from_utf8_lossy(&output.stdout).trim().to_string();
    let tgz_path = tmp.path().join(&tgz_name);
    if !tgz_path.exists() {
        return Err(color_eyre::eyre::eyre!(
            "npm pack did not produce expected file: {tgz_name}"
        ));
    }

    // Extract the tarball
    let extract_dir = tmp.path().join("extract");
    std::fs::create_dir_all(&extract_dir)?;
    let output = tokio::process::Command::new("tar")
        .args([
            "xzf",
            &tgz_path.to_string_lossy(),
            "-C",
            &extract_dir.to_string_lossy(),
        ])
        .output()
        .await
        .map_err(|e| color_eyre::eyre::eyre!("Failed to extract tarball: {e}"))?;

    if !output.status.success() {
        let stderr = String::from_utf8_lossy(&output.stderr);
        return Err(color_eyre::eyre::eyre!("tar extraction failed: {stderr}"));
    }

    // npm pack extracts to a `package/` subdirectory
    let package_dir = extract_dir.join("package");
    let source_dir = if package_dir.is_dir() {
        package_dir
    } else {
        extract_dir
    };

    install_from_local(&source_dir)
}

/// Remove a plugin by name from `~/.ava/plugins/<name>/`.
fn plugin_remove(name: &str) -> Result<()> {
    let dest = global_plugin_dir()?.join(name);

    if !dest.exists() {
        // Also check project-local
        let local = PathBuf::from(".ava/plugins").join(name);
        if local.exists() {
            std::fs::remove_dir_all(&local)?;
            println!("Removed plugin: {name} (project-local)");
            return Ok(());
        }
        return Err(color_eyre::eyre::eyre!(
            "Plugin not found: {name}\nLooked in: {}, .ava/plugins/{name}",
            dest.display()
        ));
    }

    std::fs::remove_dir_all(&dest)?;
    println!("Removed plugin: {name}");

    Ok(())
}

/// Show detailed info for a named plugin.
fn plugin_info(name: &str) -> Result<()> {
    let plugin = find_plugin_by_name(name)?;
    let m = &plugin.manifest;

    println!("Name:        {}", m.plugin.name);
    println!("Version:     {}", m.plugin.version);
    if !m.plugin.description.is_empty() {
        println!("Description: {}", m.plugin.description);
    }
    if !m.plugin.author.is_empty() {
        println!("Author:      {}", m.plugin.author);
    }
    println!("Command:     {}", m.runtime.command);
    if !m.runtime.args.is_empty() {
        println!("Args:        {}", m.runtime.args.join(" "));
    }
    if !m.runtime.env.is_empty() {
        println!("Env:");
        for (k, v) in &m.runtime.env {
            println!("  {k}={v}");
        }
    }
    if m.hooks.subscribe.is_empty() {
        println!("Hooks:       (none)");
    } else {
        println!("Hooks:       {}", m.hooks.subscribe.join(", "));
    }
    println!("Location:    {}", plugin.path.display());

    Ok(())
}

/// Format a plugin list for inline TUI display (used by `/plugin` slash command).
pub fn format_plugin_list_inline() -> String {
    let dirs = default_plugin_dirs();
    let plugins = discover_plugins(&dirs);

    if plugins.is_empty() {
        return "No plugins installed.\n\nInstall with: ava plugin add <path>".to_string();
    }

    let mut lines = Vec::new();
    for plugin in &plugins {
        let m = &plugin.manifest;
        let hooks = if m.hooks.subscribe.is_empty() {
            String::new()
        } else {
            format!(" [{}]", m.hooks.subscribe.join(", "))
        };
        lines.push(format!(
            "  {} v{}{}",
            m.plugin.name, m.plugin.version, hooks
        ));
    }
    format!("Plugins ({}):\n{}", plugins.len(), lines.join("\n"))
}

// -- helpers --

fn global_plugin_dir() -> Result<PathBuf> {
    let dir = dirs::home_dir()
        .ok_or_else(|| color_eyre::eyre::eyre!("Cannot determine home directory"))?
        .join(".ava")
        .join("plugins");
    std::fs::create_dir_all(&dir)?;
    Ok(dir)
}

fn find_plugin_by_name(name: &str) -> Result<DiscoveredPlugin> {
    let dirs = default_plugin_dirs();
    let plugins = discover_plugins(&dirs);

    plugins
        .into_iter()
        .find(|p| p.manifest.plugin.name == name)
        .ok_or_else(|| color_eyre::eyre::eyre!("Plugin not found: {name}"))
}

fn copy_dir_recursive(src: &Path, dst: &Path) -> Result<()> {
    std::fs::create_dir_all(dst)?;
    for entry in std::fs::read_dir(src)? {
        let entry = entry?;
        let ty = entry.file_type()?;
        let dest_path = dst.join(entry.file_name());
        if ty.is_dir() {
            copy_dir_recursive(&entry.path(), &dest_path)?;
        } else {
            std::fs::copy(entry.path(), &dest_path)?;
        }
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::fs;
    use std::io::Write;

    fn create_test_plugin(dir: &Path, name: &str) {
        let plugin_dir = dir.join(name);
        fs::create_dir_all(&plugin_dir).unwrap();
        let mut f = fs::File::create(plugin_dir.join("plugin.toml")).unwrap();
        write!(
            f,
            r#"
[plugin]
name = "{name}"
version = "1.0.0"
description = "Test plugin"
author = "Test"

[runtime]
command = "echo"
args = ["hello"]

[hooks]
subscribe = ["auth", "tool.before"]
"#
        )
        .unwrap();
    }

    #[test]
    fn format_inline_with_no_plugins() {
        // Just verify it doesn't panic with empty dirs
        let dirs = vec![PathBuf::from("/nonexistent/dir")];
        let plugins = discover_plugins(&dirs);
        assert!(plugins.is_empty());
    }

    #[test]
    fn copy_dir_recursive_works() {
        let tmp = tempfile::TempDir::new().unwrap();
        let src = tmp.path().join("src");
        let dst = tmp.path().join("dst");

        fs::create_dir_all(src.join("sub")).unwrap();
        fs::write(src.join("a.txt"), "hello").unwrap();
        fs::write(src.join("sub/b.txt"), "world").unwrap();

        copy_dir_recursive(&src, &dst).unwrap();

        assert!(dst.join("a.txt").exists());
        assert!(dst.join("sub/b.txt").exists());
        assert_eq!(fs::read_to_string(dst.join("a.txt")).unwrap(), "hello");
        assert_eq!(fs::read_to_string(dst.join("sub/b.txt")).unwrap(), "world");
    }

    #[test]
    fn find_plugin_in_temp_dir() {
        let tmp = tempfile::TempDir::new().unwrap();
        create_test_plugin(tmp.path(), "test-plugin");

        let plugins = discover_plugins(&[tmp.path().to_path_buf()]);
        assert_eq!(plugins.len(), 1);
        assert_eq!(plugins[0].manifest.plugin.name, "test-plugin");
        assert_eq!(plugins[0].manifest.plugin.version, "1.0.0");
    }
}
