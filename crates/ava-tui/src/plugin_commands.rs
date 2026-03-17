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
        PluginCommand::Init { name, lang } => plugin_init(&name, &lang),
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

    // If the plugin has a package.json, run npm install to resolve dependencies.
    // We use --install-strategy=nested to avoid symlinks that break when moved.
    let pkg_json = dest.join("package.json");
    if pkg_json.exists() {
        println!("Installing dependencies...");
        let npm_result = std::process::Command::new("npm")
            .args([
                "install",
                "--production",
                "--no-audit",
                "--no-fund",
                "--install-strategy=nested",
            ])
            .current_dir(&dest)
            .output();

        match npm_result {
            Ok(output) if output.status.success() => {}
            Ok(output) => {
                let stderr = String::from_utf8_lossy(&output.stderr);
                eprintln!("Warning: npm install had issues: {stderr}");
            }
            Err(e) => {
                eprintln!("Warning: could not run npm install: {e}");
            }
        }
    }

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

/// Scaffold a new plugin project.
fn plugin_init(name: &str, lang: &str) -> Result<()> {
    let dir = PathBuf::from(name);
    if dir.exists() {
        return Err(color_eyre::eyre::eyre!("Directory already exists: {name}"));
    }

    // Validate plugin name (alphanumeric, hyphens, underscores)
    if !name
        .chars()
        .all(|c| c.is_alphanumeric() || c == '-' || c == '_')
    {
        return Err(color_eyre::eyre::eyre!(
            "Invalid plugin name: {name}\nUse only alphanumeric characters, hyphens, and underscores."
        ));
    }

    std::fs::create_dir_all(&dir)?;

    match lang {
        "typescript" | "ts" => scaffold_typescript(&dir, name)?,
        "python" | "py" => scaffold_python(&dir, name)?,
        "shell" | "sh" | "bash" => scaffold_shell(&dir, name)?,
        _ => {
            // Clean up created dir on error
            let _ = std::fs::remove_dir(&dir);
            return Err(color_eyre::eyre::eyre!(
                "Unsupported language: {lang}\nSupported: typescript, python, shell"
            ));
        }
    }

    println!("Created plugin: {name}/");
    println!();
    println!("Next steps:");
    println!("  cd {name}");
    match lang {
        "typescript" | "ts" => {
            println!("  npm install");
            println!("  npx tsc");
        }
        "python" | "py" => {
            println!("  pip install -r requirements.txt  # (no deps needed)");
        }
        "shell" | "sh" | "bash" => {
            println!("  chmod +x plugin.sh");
        }
        _ => {}
    }
    println!("  ava plugin add .");
    println!("  ava plugin list");

    Ok(())
}

fn scaffold_typescript(dir: &Path, name: &str) -> Result<()> {
    std::fs::write(
        dir.join("plugin.toml"),
        format!(
            r#"[plugin]
name = "{name}"
version = "0.1.0"
description = ""
author = ""

[runtime]
command = "node"
args = ["dist/index.js"]

[hooks]
subscribe = ["session.start", "session.end"]
"#
        ),
    )?;

    std::fs::write(
        dir.join("index.ts"),
        r#"import * as fs from "node:fs";

const hooks = ["session.start", "session.end"];

interface JsonRpcMessage {
  jsonrpc: string;
  id?: number;
  method?: string;
  params?: Record<string, unknown>;
}

function sendMessage(msg: object): void {
  const json = JSON.stringify(msg);
  const header = `Content-Length: ${Buffer.byteLength(json)}\r\n\r\n`;
  fs.writeSync(1, header + json);
}

function handleMessage(msg: JsonRpcMessage): void {
  if (msg.method === "initialize") {
    process.stderr.write(`[plugin] Initialized\n`);
    sendMessage({ jsonrpc: "2.0", id: msg.id, result: { hooks } });
    return;
  }

  if (msg.method === "shutdown") {
    process.exit(0);
  }

  if (msg.method === "hook/session.start") {
    process.stderr.write(`[plugin] Session started\n`);
    if (msg.id != null) sendMessage({ jsonrpc: "2.0", id: msg.id, result: {} });
    return;
  }

  if (msg.method === "hook/session.end") {
    process.stderr.write(`[plugin] Session ended\n`);
    if (msg.id != null) sendMessage({ jsonrpc: "2.0", id: msg.id, result: {} });
    return;
  }

  // Unknown method — respond OK
  if (msg.id != null) sendMessage({ jsonrpc: "2.0", id: msg.id, result: {} });
}

let buffer = "";
process.stdin.setEncoding("utf-8");
process.stdin.on("data", (chunk: string) => {
  buffer += chunk;
  while (true) {
    const headerEnd = buffer.indexOf("\r\n\r\n");
    if (headerEnd === -1) break;
    const header = buffer.substring(0, headerEnd);
    const match = header.match(/Content-Length:\s*(\d+)/);
    if (!match) {
      buffer = buffer.substring(headerEnd + 4);
      continue;
    }
    const len = parseInt(match[1], 10);
    const bodyStart = headerEnd + 4;
    if (buffer.length < bodyStart + len) break;
    const body = buffer.substring(bodyStart, bodyStart + len);
    buffer = buffer.substring(bodyStart + len);
    try {
      handleMessage(JSON.parse(body));
    } catch (e) {
      process.stderr.write(`[plugin] Error: ${e}\n`);
    }
  }
});
"#,
    )?;

    std::fs::write(
        dir.join("package.json"),
        format!(
            r#"{{
  "name": "{name}",
  "version": "0.1.0",
  "private": true,
  "main": "dist/index.js",
  "scripts": {{
    "build": "tsc",
    "watch": "tsc --watch"
  }},
  "devDependencies": {{
    "typescript": "^5.0.0",
    "@types/node": "^20.0.0"
  }}
}}
"#
        ),
    )?;

    std::fs::write(
        dir.join("tsconfig.json"),
        r#"{
  "compilerOptions": {
    "target": "ES2020",
    "module": "commonjs",
    "outDir": "dist",
    "rootDir": ".",
    "strict": true,
    "esModuleInterop": true,
    "skipLibCheck": true,
    "forceConsistentCasingInFileNames": true
  },
  "include": ["*.ts"],
  "exclude": ["node_modules", "dist"]
}
"#,
    )?;

    std::fs::write(dir.join(".gitignore"), "node_modules/\ndist/\n")?;

    Ok(())
}

fn scaffold_python(dir: &Path, name: &str) -> Result<()> {
    std::fs::write(
        dir.join("plugin.toml"),
        format!(
            r#"[plugin]
name = "{name}"
version = "0.1.0"
description = ""
author = ""

[runtime]
command = "python3"
args = ["plugin.py"]

[hooks]
subscribe = ["session.start", "session.end"]
"#
        ),
    )?;

    std::fs::write(
        dir.join("plugin.py"),
        r#"#!/usr/bin/env python3
"""AVA plugin — JSON-RPC over stdin/stdout with Content-Length framing."""

import json
import sys

HOOKS = ["session.start", "session.end"]


def send_message(msg: dict) -> None:
    body = json.dumps(msg)
    header = f"Content-Length: {len(body.encode())}\r\n\r\n"
    sys.stdout.write(header + body)
    sys.stdout.flush()


def handle_message(msg: dict) -> None:
    method = msg.get("method", "")
    msg_id = msg.get("id")

    if method == "initialize":
        print("[plugin] Initialized", file=sys.stderr)
        send_message({"jsonrpc": "2.0", "id": msg_id, "result": {"hooks": HOOKS}})
        return

    if method == "shutdown":
        sys.exit(0)

    if method == "hook/session.start":
        print("[plugin] Session started", file=sys.stderr)
        if msg_id is not None:
            send_message({"jsonrpc": "2.0", "id": msg_id, "result": {}})
        return

    if method == "hook/session.end":
        print("[plugin] Session ended", file=sys.stderr)
        if msg_id is not None:
            send_message({"jsonrpc": "2.0", "id": msg_id, "result": {}})
        return

    # Unknown method — respond OK
    if msg_id is not None:
        send_message({"jsonrpc": "2.0", "id": msg_id, "result": {}})


def main() -> None:
    buffer = ""
    while True:
        chunk = sys.stdin.read(1)
        if not chunk:
            break
        buffer += chunk

        while "\r\n\r\n" in buffer:
            header_end = buffer.index("\r\n\r\n")
            header = buffer[:header_end]
            length_line = [
                line for line in header.split("\r\n")
                if line.startswith("Content-Length:")
            ]
            if not length_line:
                buffer = buffer[header_end + 4:]
                continue

            content_length = int(length_line[0].split(":")[1].strip())
            body_start = header_end + 4
            if len(buffer) < body_start + content_length:
                break

            body = buffer[body_start:body_start + content_length]
            buffer = buffer[body_start + content_length:]

            try:
                handle_message(json.loads(body))
            except Exception as e:
                print(f"[plugin] Error: {e}", file=sys.stderr)


if __name__ == "__main__":
    main()
"#,
    )?;

    std::fs::write(dir.join("requirements.txt"), "# No dependencies needed\n")?;

    Ok(())
}

fn scaffold_shell(dir: &Path, name: &str) -> Result<()> {
    std::fs::write(
        dir.join("plugin.toml"),
        format!(
            r#"[plugin]
name = "{name}"
version = "0.1.0"
description = ""
author = ""

[runtime]
command = "bash"
args = ["plugin.sh"]

[hooks]
subscribe = ["session.start", "session.end"]
"#
        ),
    )?;

    std::fs::write(
        dir.join("plugin.sh"),
        r#"#!/usr/bin/env bash
# AVA plugin — JSON-RPC over stdin/stdout with Content-Length framing.
set -euo pipefail

send_response() {
  local body="$1"
  local len=${#body}
  printf "Content-Length: %d\r\n\r\n%s" "$len" "$body"
}

handle_message() {
  local msg="$1"
  local method id

  method=$(echo "$msg" | jq -r '.method // empty')
  id=$(echo "$msg" | jq -r '.id // empty')

  case "$method" in
    initialize)
      echo "[plugin] Initialized" >&2
      send_response "{\"jsonrpc\":\"2.0\",\"id\":$id,\"result\":{\"hooks\":[\"session.start\",\"session.end\"]}}"
      ;;
    shutdown)
      exit 0
      ;;
    hook/session.start)
      echo "[plugin] Session started" >&2
      [ -n "$id" ] && send_response "{\"jsonrpc\":\"2.0\",\"id\":$id,\"result\":{}}"
      ;;
    hook/session.end)
      echo "[plugin] Session ended" >&2
      [ -n "$id" ] && send_response "{\"jsonrpc\":\"2.0\",\"id\":$id,\"result\":{}}"
      ;;
    *)
      [ -n "$id" ] && send_response "{\"jsonrpc\":\"2.0\",\"id\":$id,\"result\":{}}"
      ;;
  esac
}

# Read Content-Length framed messages from stdin
while IFS= read -r line; do
  if [[ "$line" =~ Content-Length:\ ([0-9]+) ]]; then
    content_length="${BASH_REMATCH[1]}"
    read -r _blank  # consume \r\n
    body=$(dd bs=1 count="$content_length" 2>/dev/null)
    handle_message "$body"
  fi
done
"#,
    )?;

    Ok(())
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

/// Directories to skip when copying plugin files.
const SKIP_DIRS: &[&str] = &["node_modules", ".git", "target", "__pycache__", ".tox"];

fn copy_dir_recursive(src: &Path, dst: &Path) -> Result<()> {
    std::fs::create_dir_all(dst)?;
    for entry in std::fs::read_dir(src)? {
        let entry = entry?;
        let name = entry.file_name();
        let name_str = name.to_string_lossy();
        let dest_path = dst.join(&name);
        let ty = entry.file_type()?;

        if ty.is_dir() {
            if SKIP_DIRS.iter().any(|s| *s == name_str.as_ref()) {
                continue;
            }
            copy_dir_recursive(&entry.path(), &dest_path)?;
        } else if ty.is_file() {
            std::fs::copy(entry.path(), &dest_path)?;
        }
        // Skip symlinks and other special file types
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
