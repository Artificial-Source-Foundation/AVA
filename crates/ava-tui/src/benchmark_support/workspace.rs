use std::path::Path;

use ava_config::trust_project;
use color_eyre::eyre::{eyre, Result};

pub(crate) async fn prepare_benchmark_workspace(workspace_dir: &Path) -> Result<()> {
    trust_project(workspace_dir)
        .map_err(|e| eyre!("Failed to trust benchmark workspace: {}", e))?;

    let ava_dir = workspace_dir.join(".ava");
    tokio::fs::create_dir_all(&ava_dir)
        .await
        .map_err(|e| eyre!("Failed to create benchmark .ava dir: {}", e))?;
    let agents_toml = ava_dir.join("agents.toml");
    let agents_config = r#"
[defaults]
enabled = true

[agents.scout]
enabled = true
max_turns = 5

[agents.explore]
enabled = true
max_turns = 5

[agents.plan]
enabled = true
max_turns = 6

[agents.review]
enabled = true
max_turns = 6

[agents.worker]
enabled = true
max_turns = 10

[agents.task]
enabled = true
max_turns = 10
"#;
    tokio::fs::write(&agents_toml, agents_config.trim_start())
        .await
        .map_err(|e| eyre!("Failed to write benchmark agents.toml: {}", e))?;

    Ok(())
}

pub(crate) fn expected_min_subagents(task_name: &str) -> Option<u32> {
    match task_name {
        "delegated_config_bugfix" => Some(1),
        _ => None,
    }
}

pub(crate) fn subagent_type_from_description(description: &str) -> String {
    if let Some(stripped) = description.strip_prefix('[') {
        if let Some((agent_type, _)) = stripped.split_once(']') {
            let agent_type = agent_type.trim();
            if !agent_type.is_empty() {
                return agent_type.to_string();
            }
        }
    }
    "task".to_string()
}

pub(crate) async fn setup_agentic_file(
    temp_dir: &Path,
    task_name: &str,
    setup_code: &str,
) -> Result<()> {
    match task_name {
        "bugfix_off_by_one" => {
            let path = temp_dir.join("binary_search.rs");
            tokio::fs::write(&path, setup_code)
                .await
                .map_err(|e| eyre!("Failed to write setup file {}: {}", path.display(), e))?;
        }
        "bugfix_lifetime" => {
            let path = temp_dir.join("lifetime_fix.rs");
            tokio::fs::write(&path, setup_code)
                .await
                .map_err(|e| eyre!("Failed to write setup file {}: {}", path.display(), e))?;
        }
        "refactor_extract" => {
            let path = temp_dir.join("refactor.rs");
            tokio::fs::write(&path, setup_code)
                .await
                .map_err(|e| eyre!("Failed to write setup file {}: {}", path.display(), e))?;
        }
        "multi_step_debug" => {
            let dir = temp_dir.join("multi_step_debug");
            tokio::fs::create_dir_all(&dir)
                .await
                .map_err(|e| eyre!("Failed to create dir {}: {}", dir.display(), e))?;
            let lib_path = dir.join("lib.rs");
            tokio::fs::write(&lib_path, setup_code)
                .await
                .map_err(|e| eyre!("Failed to write {}: {}", lib_path.display(), e))?;
            let tests_path = dir.join("tests.rs");
            let test_content = r#"
mod lib;

#[test]
fn test_area() {
    assert!((lib::area(3.0, 4.0) - 12.0).abs() < 1e-9);
}

#[test]
fn test_perimeter() {
    assert!((lib::perimeter(3.0, 4.0) - 14.0).abs() < 1e-9);
}

#[test]
fn test_diagonal() {
    assert!((lib::diagonal(3.0, 4.0) - 5.0).abs() < 1e-9);
}
"#;
            tokio::fs::write(&tests_path, test_content)
                .await
                .map_err(|e| eyre!("Failed to write {}: {}", tests_path.display(), e))?;
        }
        "constraint_edit" => {
            let path = temp_dir.join("validators.rs");
            tokio::fs::write(&path, setup_code)
                .await
                .map_err(|e| eyre!("Failed to write {}: {}", path.display(), e))?;
        }
        "self_correct_compile" => {
            let path = temp_dir.join("cache.rs");
            tokio::fs::write(&path, setup_code)
                .await
                .map_err(|e| eyre!("Failed to write {}: {}", path.display(), e))?;
        }
        "tool_efficiency" => {
            let src_dir = temp_dir.join("tool_efficiency").join("src");
            tokio::fs::create_dir_all(&src_dir)
                .await
                .map_err(|e| eyre!("Failed to create dir {}: {}", src_dir.display(), e))?;

            let main_code = "mod lib;\n\nfn main() {\n    let cfg = lib::config::Config::default();\n    let msg = lib::utils::greet(&cfg.name);\n    println!(\"{}\", msg);\n}\n";
            let lib_code = "pub mod utils;\npub mod config;\n";
            let utils_code = "/// Greets a user by name.\npub fn greet(name: &str) -> String {\n    format!(\"Hello, {}!\", name)\n}\n\n/// Formats a duration in seconds into a human-readable string.\npub fn format_duration(seconds: u64) -> String {\n    if seconds < 60 {\n        format!(\"{}s\", seconds)\n    } else if seconds < 3600 {\n        format!(\"{}m {}s\", seconds / 60, seconds % 60)\n    } else {\n        format!(\"{}h {}m\", seconds / 3600, (seconds % 3600) / 60)\n    }\n}\n";

            tokio::fs::write(src_dir.join("main.rs"), main_code)
                .await
                .map_err(|e| eyre!("Failed to write main.rs: {}", e))?;
            tokio::fs::write(src_dir.join("lib.rs"), lib_code)
                .await
                .map_err(|e| eyre!("Failed to write lib.rs: {}", e))?;
            tokio::fs::write(src_dir.join("utils.rs"), utils_code)
                .await
                .map_err(|e| eyre!("Failed to write utils.rs: {}", e))?;
            tokio::fs::write(src_dir.join("config.rs"), setup_code)
                .await
                .map_err(|e| eyre!("Failed to write config.rs: {}", e))?;
        }
        "no_overengineer" => {
            let path = temp_dir.join("math.rs");
            tokio::fs::write(&path, setup_code)
                .await
                .map_err(|e| eyre!("Failed to write {}: {}", path.display(), e))?;
        }
        "error_recovery_loop" => {
            let path = temp_dir.join("broken.rs");
            tokio::fs::write(&path, setup_code)
                .await
                .map_err(|e| eyre!("Failed to write {}: {}", path.display(), e))?;
        }
        "rule_guided_typescript" => {
            let frontend_dir = temp_dir.join("frontend");
            let rules_dir = temp_dir.join(".ava").join("rules");
            tokio::fs::create_dir_all(&frontend_dir)
                .await
                .map_err(|e| eyre!("Failed to create dir {}: {}", frontend_dir.display(), e))?;
            tokio::fs::create_dir_all(&rules_dir)
                .await
                .map_err(|e| eyre!("Failed to create dir {}: {}", rules_dir.display(), e))?;
            tokio::fs::write(frontend_dir.join("app.ts"), setup_code)
                .await
                .map_err(|e| eyre!("Failed to write frontend/app.ts: {}", e))?;
            let rule_text = r#"---
paths:
  - "**/*.ts"
---
For TypeScript files:
- Use strict equality (`===` / `!==`) instead of loose equality.
- Do not use semicolons.
- Keep exported types and function signatures stable.
"#;
            tokio::fs::write(rules_dir.join("typescript-style.md"), rule_text)
                .await
                .map_err(|e| eyre!("Failed to write benchmark TypeScript rule: {}", e))?;
        }
        "delegated_config_bugfix" => {
            let dir = temp_dir.join("delegated_config_bugfix");
            tokio::fs::create_dir_all(&dir)
                .await
                .map_err(|e| eyre!("Failed to create dir {}: {}", dir.display(), e))?;
            tokio::fs::write(dir.join("config.rs"), setup_code)
                .await
                .map_err(|e| eyre!("Failed to write config.rs: {}", e))?;
            let client_code = r#"use crate::config::Config;

pub struct Client {
    retry_limit: usize,
    timeout_ms: u64,
}

impl Client {
    pub fn new(_config: &Config) -> Self {
        Self {
            retry_limit: 1,
            timeout_ms: 1000,
        }
    }

    pub fn retry_limit(&self) -> usize {
        self.retry_limit
    }

    pub fn timeout_ms(&self) -> u64 {
        self.timeout_ms
    }
}
"#;
            let tests_code = r#"mod config;
mod client;

use client::Client;
use config::Config;

#[test]
fn client_uses_configured_retry_limit() {
    let config = Config {
        retry_limit: 5,
        timeout_ms: 9000,
    };
    let client = Client::new(&config);
    assert_eq!(client.retry_limit(), 5);
}

#[test]
fn client_uses_configured_timeout() {
    let config = Config {
        retry_limit: 2,
        timeout_ms: 4200,
    };
    let client = Client::new(&config);
    assert_eq!(client.timeout_ms(), 4200);
}
"#;
            tokio::fs::write(dir.join("client.rs"), client_code)
                .await
                .map_err(|e| eyre!("Failed to write client.rs: {}", e))?;
            tokio::fs::write(dir.join("tests.rs"), tests_code)
                .await
                .map_err(|e| eyre!("Failed to write tests.rs: {}", e))?;
        }
        _ => return Ok(()),
    }

    Ok(())
}
