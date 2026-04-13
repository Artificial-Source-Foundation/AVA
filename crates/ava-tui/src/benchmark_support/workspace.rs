use std::path::Path;

use ava_config::trust_project;
use ava_tools::core::path_guard::{activate_workspace_override, WorkspaceOverrideGuard};
use color_eyre::eyre::{eyre, Result};
use serde_json::json;

pub(crate) struct BenchmarkWorkspaceGuard {
    _inner: WorkspaceOverrideGuard,
}

impl BenchmarkWorkspaceGuard {
    pub(crate) fn activate(workspace_dir: &Path) -> Self {
        let canonical = workspace_dir
            .canonicalize()
            .unwrap_or_else(|_| workspace_dir.to_path_buf());
        Self {
            _inner: activate_workspace_override(canonical),
        }
    }
}

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

    prepare_mcp_benchmark_fixtures(workspace_dir).await?;
    prepare_lsp_smoke_fixtures(workspace_dir).await?;
    prepare_product_smoke_fixtures(workspace_dir).await?;

    Ok(())
}

async fn prepare_lsp_smoke_fixtures(workspace_dir: &Path) -> Result<()> {
    let benchmark_lsp_dir = workspace_dir.join("benchmark_lsp");
    if benchmark_lsp_dir.exists() {
        tokio::fs::remove_dir_all(&benchmark_lsp_dir)
            .await
            .map_err(|e| eyre!("Failed to reset LSP smoke fixture dir: {}", e))?;
    }

    let reports_dir = benchmark_lsp_dir.join("reports");
    let config_dir = benchmark_lsp_dir.join("config_disabled").join(".ava");
    let rust_dir = benchmark_lsp_dir
        .join("project_matrix")
        .join("rust_service");
    let ts_dir = benchmark_lsp_dir.join("project_matrix").join("ts_app");
    let py_dir = benchmark_lsp_dir
        .join("project_matrix")
        .join("python_worker");

    for dir in [&reports_dir, &config_dir, &rust_dir, &ts_dir, &py_dir] {
        tokio::fs::create_dir_all(dir).await.map_err(|e| {
            eyre!(
                "Failed to create LSP smoke fixture dir {}: {}",
                dir.display(),
                e
            )
        })?;
    }

    let config_yaml = r#"features:
  enable_lsp: false
  enable_mcp: true
"#;
    tokio::fs::write(config_dir.join("config.yaml"), config_yaml)
        .await
        .map_err(|e| eyre!("Failed to write LSP config gate fixture: {}", e))?;

    tokio::fs::write(
        rust_dir.join("Cargo.toml"),
        "[package]\nname = \"rust_service\"\nversion = \"0.1.0\"\nedition = \"2021\"\n",
    )
    .await
    .map_err(|e| eyre!("Failed to write rust_service Cargo.toml fixture: {}", e))?;

    tokio::fs::write(
        rust_dir.join("rust-toolchain.toml"),
        "[toolchain]\nchannel = \"stable\"\n",
    )
    .await
    .map_err(|e| eyre!("Failed to write rust_service toolchain fixture: {}", e))?;

    tokio::fs::write(
        ts_dir.join("package.json"),
        "{\n  \"name\": \"ts_app\",\n  \"private\": true\n}\n",
    )
    .await
    .map_err(|e| eyre!("Failed to write ts_app package.json fixture: {}", e))?;

    tokio::fs::write(
        ts_dir.join("tsconfig.json"),
        "{\n  \"compilerOptions\": {\n    \"target\": \"ES2022\"\n  }\n}\n",
    )
    .await
    .map_err(|e| eyre!("Failed to write ts_app tsconfig fixture: {}", e))?;

    tokio::fs::write(
        py_dir.join("pyproject.toml"),
        "[project]\nname = \"python_worker\"\nversion = \"0.1.0\"\n",
    )
    .await
    .map_err(|e| eyre!("Failed to write python_worker pyproject fixture: {}", e))?;

    let known_servers = "rust-analyzer\ntypescript\neslint\nbiome\npython\ngopls\nclangd\n";
    tokio::fs::write(
        benchmark_lsp_dir.join("known_servers_snapshot.txt"),
        known_servers,
    )
    .await
    .map_err(|e| eyre!("Failed to write known server snapshot fixture: {}", e))?;

    Ok(())
}

async fn prepare_product_smoke_fixtures(workspace_dir: &Path) -> Result<()> {
    let benchmark_product_dir = workspace_dir.join("benchmark_product");
    if benchmark_product_dir.exists() {
        tokio::fs::remove_dir_all(&benchmark_product_dir)
            .await
            .map_err(|e| eyre!("Failed to reset product smoke fixture dir: {}", e))?;
    }

    let sessions_dir = benchmark_product_dir.join("sessions");
    let config_dir = benchmark_product_dir.join("config");
    let permissions_dir = benchmark_product_dir.join("permissions");
    let tools_dir = benchmark_product_dir.join("tools");
    let reports_dir = benchmark_product_dir.join("reports");

    for dir in [
        &sessions_dir,
        &config_dir,
        &permissions_dir,
        &tools_dir,
        &reports_dir,
    ] {
        tokio::fs::create_dir_all(dir).await.map_err(|e| {
            eyre!(
                "Failed to create product smoke fixture dir {}: {}",
                dir.display(),
                e
            )
        })?;
    }

    let session_index = json!([
        {
            "id": "sess_001",
            "updated_at": "2026-04-08T10:15:00Z",
            "message_count": 12
        },
        {
            "id": "sess_002",
            "updated_at": "2026-04-09T09:30:00Z",
            "message_count": 0
        }
    ]);
    tokio::fs::write(
        sessions_dir.join("session_index.json"),
        format!(
            "{}\n",
            serde_json::to_string_pretty(&session_index)
                .map_err(|e| eyre!("Failed to serialize product session fixture: {}", e))?
        ),
    )
    .await
    .map_err(|e| eyre!("Failed to write product session fixture: {}", e))?;

    let default_profile = json!({
        "provider": "openrouter",
        "model": "anthropic/claude-haiku-4.5"
    });
    tokio::fs::write(
        config_dir.join("default_profile.json"),
        format!(
            "{}\n",
            serde_json::to_string_pretty(&default_profile)
                .map_err(|e| eyre!("Failed to serialize product profile fixture: {}", e))?
        ),
    )
    .await
    .map_err(|e| eyre!("Failed to write product profile fixture: {}", e))?;

    let permission_policy = json!({
        "default_decision": "ask",
        "allow": ["read", "glob"],
        "deny_patterns": ["rm -rf", "curl | sh"]
    });
    tokio::fs::write(
        permissions_dir.join("policy.json"),
        format!(
            "{}\n",
            serde_json::to_string_pretty(&permission_policy)
                .map_err(|e| eyre!("Failed to serialize permission policy fixture: {}", e))?
        ),
    )
    .await
    .map_err(|e| eyre!("Failed to write permission policy fixture: {}", e))?;

    let permission_requests = json!([
        {"id": "req_read", "tool": "read", "target": "README.md"},
        {"id": "req_delete", "tool": "bash", "command": "rm -rf /tmp/demo"},
        {"id": "req_edit", "tool": "edit", "target": "src/lib.rs"}
    ]);
    tokio::fs::write(
        permissions_dir.join("requests.json"),
        format!(
            "{}\n",
            serde_json::to_string_pretty(&permission_requests)
                .map_err(|e| eyre!("Failed to serialize permission requests fixture: {}", e))?
        ),
    )
    .await
    .map_err(|e| eyre!("Failed to write permission requests fixture: {}", e))?;

    let tool_registry = json!({
        "available_tools": ["read", "glob", "edit", "bash", "write", "web_fetch"]
    });
    tokio::fs::write(
        tools_dir.join("registry.json"),
        format!(
            "{}\n",
            serde_json::to_string_pretty(&tool_registry)
                .map_err(|e| eyre!("Failed to serialize tool registry fixture: {}", e))?
        ),
    )
    .await
    .map_err(|e| eyre!("Failed to write tool registry fixture: {}", e))?;

    let tool_policy = json!({
        "blocked_tools": ["write", "web_fetch"],
        "requires_approval": ["bash"]
    });
    tokio::fs::write(
        tools_dir.join("tool_policy.json"),
        format!(
            "{}\n",
            serde_json::to_string_pretty(&tool_policy)
                .map_err(|e| eyre!("Failed to serialize tool policy fixture: {}", e))?
        ),
    )
    .await
    .map_err(|e| eyre!("Failed to write tool policy fixture: {}", e))?;

    Ok(())
}

async fn prepare_mcp_benchmark_fixtures(workspace_dir: &Path) -> Result<()> {
    let python_check = tokio::process::Command::new("python3")
        .arg("--version")
        .output()
        .await
        .map_err(|e| eyre!("python3 is required for MCP benchmark fixtures: {}", e))?;
    if !python_check.status.success() {
        return Err(eyre!(
            "python3 is required for MCP benchmark fixtures but `python3 --version` failed"
        ));
    }

    let benchmark_mcp_dir = workspace_dir.join("benchmark_mcp");
    if benchmark_mcp_dir.exists() {
        tokio::fs::remove_dir_all(&benchmark_mcp_dir)
            .await
            .map_err(|e| eyre!("Failed to reset MCP benchmark dir: {}", e))?;
    }

    let servers_dir = benchmark_mcp_dir.join("servers");
    let logs_dir = benchmark_mcp_dir.join("logs");
    let fs_root = benchmark_mcp_dir.join("filesystem_root");
    let fs_inbox = fs_root.join("inbox");
    let fs_reports = fs_root.join("reports");
    let git_repo = benchmark_mcp_dir.join("git_repo");

    for dir in [
        &servers_dir,
        &logs_dir,
        &fs_root,
        &fs_inbox,
        &fs_reports,
        &git_repo,
    ] {
        tokio::fs::create_dir_all(dir)
            .await
            .map_err(|e| eyre!("Failed to create MCP fixture dir {}: {}", dir.display(), e))?;
    }

    tokio::fs::write(
        fs_inbox.join("todo.txt"),
        "refactor parser\npatch flaky benchmark\nwrite release notes\n",
    )
    .await
    .map_err(|e| eyre!("Failed to write MCP todo fixture: {}", e))?;

    tokio::fs::write(fs_inbox.join("phrase.txt"), "ava mcp integration benchmark")
        .await
        .map_err(|e| eyre!("Failed to write MCP phrase fixture: {}", e))?;

    for log_name in ["fs_audit.jsonl", "git_audit.jsonl", "textops_audit.jsonl"] {
        tokio::fs::write(logs_dir.join(log_name), "")
            .await
            .map_err(|e| eyre!("Failed to initialize MCP audit log {}: {}", log_name, e))?;
    }

    let server_script = servers_dir.join("mock_mcp_server.py");
    tokio::fs::write(&server_script, MOCK_MCP_SERVER_SCRIPT)
        .await
        .map_err(|e| eyre!("Failed to write MCP mock server script: {}", e))?;

    initialize_git_fixture_repo(&git_repo).await?;
    write_benchmark_mcp_config(
        workspace_dir,
        &server_script,
        &logs_dir,
        &fs_root,
        &git_repo,
    )
    .await?;

    Ok(())
}

async fn initialize_git_fixture_repo(repo_dir: &Path) -> Result<()> {
    tokio::fs::write(repo_dir.join("README.md"), "# MCP benchmark git fixture\n")
        .await
        .map_err(|e| eyre!("Failed to write git fixture README: {}", e))?;

    run_git(repo_dir, &["init"]).await?;
    run_git(repo_dir, &["config", "user.name", "AVA Benchmark Bot"]).await?;
    run_git(repo_dir, &["config", "user.email", "benchmark@ava.local"]).await?;
    run_git(repo_dir, &["add", "README.md"]).await?;
    run_git(repo_dir, &["commit", "-m", "chore: initial fixture"]).await?;
    Ok(())
}

async fn run_git(repo_dir: &Path, args: &[&str]) -> Result<()> {
    let output = tokio::process::Command::new("git")
        .args(args)
        .current_dir(repo_dir)
        .output()
        .await
        .map_err(|e| eyre!("Failed to run git {:?}: {}", args, e))?;

    if output.status.success() {
        return Ok(());
    }

    let stderr = String::from_utf8_lossy(&output.stderr);
    let stdout = String::from_utf8_lossy(&output.stdout);
    Err(eyre!(
        "git {:?} failed in {}\nstdout: {}\nstderr: {}",
        args,
        repo_dir.display(),
        stdout.trim(),
        stderr.trim()
    ))
}

async fn write_benchmark_mcp_config(
    workspace_dir: &Path,
    server_script: &Path,
    logs_dir: &Path,
    fs_root: &Path,
    git_repo: &Path,
) -> Result<()> {
    let mcp_path = workspace_dir.join(".ava").join("mcp.json");

    let script_path = server_script.to_string_lossy().to_string();
    let fs_root_str = fs_root.to_string_lossy().to_string();
    let git_repo_str = git_repo.to_string_lossy().to_string();
    let fs_log = logs_dir
        .join("fs_audit.jsonl")
        .to_string_lossy()
        .to_string();
    let git_log = logs_dir
        .join("git_audit.jsonl")
        .to_string_lossy()
        .to_string();
    let textops_log = logs_dir
        .join("textops_audit.jsonl")
        .to_string_lossy()
        .to_string();

    let mcp = json!({
        "servers": [
            {
                "name": "fs",
                "enabled": true,
                "transport": {
                    "type": "stdio",
                    "command": "python3",
                    "args": ["-u", script_path.clone(), "--mode", "filesystem", "--root", fs_root_str],
                    "env": {
                        "AVA_MCP_AUDIT_LOG": fs_log
                    }
                }
            },
            {
                "name": "git",
                "enabled": true,
                "transport": {
                    "type": "stdio",
                    "command": "python3",
                    "args": ["-u", script_path.clone(), "--mode", "git", "--repo", git_repo_str],
                    "env": {
                        "AVA_MCP_AUDIT_LOG": git_log
                    }
                }
            },
            {
                "name": "textops",
                "enabled": true,
                "transport": {
                    "type": "stdio",
                    "command": "python3",
                    "args": ["-u", script_path, "--mode", "textops"],
                    "env": {
                        "AVA_MCP_AUDIT_LOG": textops_log
                    }
                }
            }
        ]
    });

    let body = serde_json::to_string_pretty(&mcp)
        .map_err(|e| eyre!("Failed to serialize benchmark mcp.json: {}", e))?;
    tokio::fs::write(&mcp_path, format!("{body}\n"))
        .await
        .map_err(|e| eyre!("Failed to write benchmark mcp.json: {}", e))?;

    Ok(())
}

const MOCK_MCP_SERVER_SCRIPT: &str = r#"#!/usr/bin/env python3
import argparse
import hashlib
import json
import os
import subprocess
import sys
from pathlib import Path


def read_msg():
    line = sys.stdin.readline()
    if not line:
        return None
    return json.loads(line)


def write_msg(payload):
    sys.stdout.write(json.dumps(payload) + "\n")
    sys.stdout.flush()


def append_audit(server, tool, arguments):
    audit_path = os.environ.get("AVA_MCP_AUDIT_LOG")
    if not audit_path:
        return
    payload = {
        "server": server,
        "tool": tool,
        "arguments": arguments,
    }
    with open(audit_path, "a", encoding="utf-8") as f:
        f.write(json.dumps(payload) + "\n")


def as_mcp_result(value):
    if isinstance(value, str):
        return {"content": [{"type": "text", "text": value}], "isError": False}
    return {"content": [{"type": "text", "text": json.dumps(value)}], "isError": False}


def mcp_error(message):
    return {"content": [{"type": "text", "text": message}], "isError": True}


def within_root(root: Path, raw_path: str) -> Path:
    candidate = (root / raw_path).resolve()
    if not str(candidate).startswith(str(root.resolve())):
        raise ValueError("path escapes root")
    return candidate


def filesystem_tools(root: Path):
    def list_files(arguments):
        rel = arguments.get("path", ".")
        target = within_root(root, rel)
        if not target.exists() or not target.is_dir():
            return mcp_error(f"directory not found: {rel}")
        entries = sorted([p.name + ("/" if p.is_dir() else "") for p in target.iterdir()])
        return as_mcp_result("\n".join(entries))

    def read_text(arguments):
        rel = arguments.get("path", "")
        target = within_root(root, rel)
        if not target.exists() or not target.is_file():
            return mcp_error(f"file not found: {rel}")
        return as_mcp_result(target.read_text(encoding="utf-8"))

    def write_text(arguments):
        rel = arguments.get("path", "")
        content = arguments.get("content", "")
        target = within_root(root, rel)
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(content, encoding="utf-8")
        return as_mcp_result(f"wrote {rel}")

    return {
        "list_files": {
            "description": "List files under root",
            "schema": {"type": "object", "properties": {"path": {"type": "string"}}},
            "call": list_files,
        },
        "read_text": {
            "description": "Read UTF-8 text file under root",
            "schema": {
                "type": "object",
                "properties": {"path": {"type": "string"}},
                "required": ["path"],
            },
            "call": read_text,
        },
        "write_text": {
            "description": "Write UTF-8 text file under root",
            "schema": {
                "type": "object",
                "properties": {
                    "path": {"type": "string"},
                    "content": {"type": "string"},
                },
                "required": ["path", "content"],
            },
            "call": write_text,
        },
    }


def git_tools(repo: Path):
    def run_git(args):
        result = subprocess.run(
            ["git", *args],
            cwd=str(repo),
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            check=False,
        )
        if result.returncode != 0:
            return mcp_error(result.stderr.strip() or result.stdout.strip() or "git command failed")
        return as_mcp_result(result.stdout.strip())

    def write_file(arguments):
        rel = arguments.get("path", "")
        content = arguments.get("content", "")
        target = within_root(repo, rel)
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(content, encoding="utf-8")
        return as_mcp_result(f"wrote {rel}")

    def status(_arguments):
        return run_git(["status", "--short"])

    def add(arguments):
        path = arguments.get("path", ".")
        return run_git(["add", path])

    def commit(arguments):
        message = arguments.get("message", "")
        if not message:
            return mcp_error("message is required")
        return run_git(["commit", "-m", message])

    def log(arguments):
        limit = int(arguments.get("limit", 1))
        return run_git(["log", f"-{limit}", "--pretty=format:%H %s"])

    return {
        "write_file": {
            "description": "Write file in repo",
            "schema": {
                "type": "object",
                "properties": {
                    "path": {"type": "string"},
                    "content": {"type": "string"},
                },
                "required": ["path", "content"],
            },
            "call": write_file,
        },
        "status": {
            "description": "git status --short",
            "schema": {"type": "object", "properties": {}},
            "call": status,
        },
        "add": {
            "description": "git add <path>",
            "schema": {
                "type": "object",
                "properties": {"path": {"type": "string"}},
                "required": ["path"],
            },
            "call": add,
        },
        "commit": {
            "description": "git commit -m <message>",
            "schema": {
                "type": "object",
                "properties": {"message": {"type": "string"}},
                "required": ["message"],
            },
            "call": commit,
        },
        "log": {
            "description": "git log --pretty=format:%H %s",
            "schema": {
                "type": "object",
                "properties": {"limit": {"type": "integer"}},
            },
            "call": log,
        },
    }


def textops_tools():
    def sha256_text(arguments):
        text = arguments.get("text", "")
        digest = hashlib.sha256(text.encode("utf-8")).hexdigest()
        return as_mcp_result(digest)

    def word_count(arguments):
        text = arguments.get("text", "")
        count = len([w for w in text.split() if w])
        return as_mcp_result(str(count))

    return {
        "sha256_text": {
            "description": "Compute SHA-256 hex digest for input text",
            "schema": {
                "type": "object",
                "properties": {"text": {"type": "string"}},
                "required": ["text"],
            },
            "call": sha256_text,
        },
        "word_count": {
            "description": "Count words in input text",
            "schema": {
                "type": "object",
                "properties": {"text": {"type": "string"}},
                "required": ["text"],
            },
            "call": word_count,
        },
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--mode", required=True, choices=["filesystem", "git", "textops"])
    parser.add_argument("--root")
    parser.add_argument("--repo")
    args = parser.parse_args()

    if args.mode == "filesystem":
        if not args.root:
            raise RuntimeError("--root is required for filesystem mode")
        tools = filesystem_tools(Path(args.root))
        server_name = "fs"
    elif args.mode == "git":
        if not args.repo:
            raise RuntimeError("--repo is required for git mode")
        tools = git_tools(Path(args.repo))
        server_name = "git"
    else:
        tools = textops_tools()
        server_name = "textops"

    while True:
        message = read_msg()
        if message is None:
            break

        method = message.get("method")
        request_id = message.get("id")

        if method == "initialize":
            write_msg(
                {
                    "jsonrpc": "2.0",
                    "id": request_id,
                    "result": {
                        "protocolVersion": "2024-11-05",
                        "capabilities": {"tools": {}},
                        "serverInfo": {"name": f"mock-{server_name}", "version": "1.0"},
                    },
                }
            )
            continue

        if method == "notifications/initialized":
            continue

        if method == "tools/list":
            payload_tools = []
            for name, meta in tools.items():
                payload_tools.append(
                    {
                        "name": name,
                        "description": meta["description"],
                        "inputSchema": meta["schema"],
                    }
                )
            write_msg(
                {
                    "jsonrpc": "2.0",
                    "id": request_id,
                    "result": {"tools": payload_tools},
                }
            )
            continue

        if method == "tools/call":
            params = message.get("params", {})
            tool_name = params.get("name", "")
            arguments = params.get("arguments", {})
            append_audit(server_name, tool_name, arguments)
            if tool_name not in tools:
                result = mcp_error(f"unknown tool: {tool_name}")
            else:
                try:
                    result = tools[tool_name]["call"](arguments)
                except Exception as exc:  # deterministic local fixture; return structured error
                    result = mcp_error(str(exc))

            write_msg({"jsonrpc": "2.0", "id": request_id, "result": result})
            continue

        if request_id is not None:
            write_msg(
                {
                    "jsonrpc": "2.0",
                    "id": request_id,
                    "error": {
                        "code": -32601,
                        "message": f"method not found: {method}",
                    },
                }
            )


if __name__ == "__main__":
    main()
"#;

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
        "tool_reliability_timeout" => {
            let dir = temp_dir.join("tool_reliability_timeout");
            tokio::fs::create_dir_all(&dir)
                .await
                .map_err(|e| eyre!("Failed to create dir {}: {}", dir.display(), e))?;
            tokio::fs::write(dir.join("config.rs"), setup_code)
                .await
                .map_err(|e| eyre!("Failed to write config.rs: {}", e))?;
            let tests_code = r#"mod config;

use config::AppConfig;

#[test]
fn default_timeout_is_updated() {
    assert_eq!(AppConfig::default().timeout_seconds, 30);
}

#[test]
fn retry_limit_is_unchanged() {
    assert_eq!(AppConfig::default().retry_limit, 2);
}
"#;
            tokio::fs::write(dir.join("tests.rs"), tests_code)
                .await
                .map_err(|e| eyre!("Failed to write tests.rs: {}", e))?;
        }
        "tool_reliability_log_filter" => {
            let dir = temp_dir.join("tool_reliability_log_filter");
            tokio::fs::create_dir_all(&dir)
                .await
                .map_err(|e| eyre!("Failed to create dir {}: {}", dir.display(), e))?;
            tokio::fs::write(dir.join("logger.rs"), setup_code)
                .await
                .map_err(|e| eyre!("Failed to write logger.rs: {}", e))?;
            let tests_code = r#"mod logger;

use logger::should_log;

#[test]
fn keeps_error_logs_when_not_verbose() {
    assert!(should_log("error", false));
}

#[test]
fn keeps_warn_logs_when_not_verbose() {
    assert!(should_log("warn", false));
}

#[test]
fn keeps_all_logs_when_verbose() {
    assert!(should_log("info", true));
}
"#;
            tokio::fs::write(dir.join("tests.rs"), tests_code)
                .await
                .map_err(|e| eyre!("Failed to write tests.rs: {}", e))?;
        }
        "tool_reliability_normalize" => {
            let dir = temp_dir.join("tool_reliability_normalize");
            tokio::fs::create_dir_all(&dir)
                .await
                .map_err(|e| eyre!("Failed to create dir {}: {}", dir.display(), e))?;
            tokio::fs::write(dir.join("normalize.rs"), setup_code)
                .await
                .map_err(|e| eyre!("Failed to write normalize.rs: {}", e))?;
            let tests_code = r#"mod normalize;

use normalize::normalize_user_id;

#[test]
fn trims_outer_whitespace() {
    assert_eq!(normalize_user_id("  Alice  "), "alice");
}

#[test]
fn collapses_internal_spaces_to_single_dashes() {
    assert_eq!(normalize_user_id("Alice Smith"), "alice-smith");
}

#[test]
fn strips_duplicate_dashes() {
    assert_eq!(normalize_user_id("Alice   Smith"), "alice-smith");
}
"#;
            tokio::fs::write(dir.join("tests.rs"), tests_code)
                .await
                .map_err(|e| eyre!("Failed to write tests.rs: {}", e))?;
        }
        "tool_recovery_missing_file" => {
            let dir = temp_dir.join("tool_recovery_missing_file");
            tokio::fs::create_dir_all(&dir)
                .await
                .map_err(|e| eyre!("Failed to create dir {}: {}", dir.display(), e))?;
            tokio::fs::write(dir.join("formatter.rs"), setup_code)
                .await
                .map_err(|e| eyre!("Failed to write formatter.rs: {}", e))?;
            let tests_code = r#"mod formatter;
mod slug;

use formatter::format_ticket_slug;

#[test]
fn creates_lowercase_ticket_slug() {
    assert_eq!(format_ticket_slug("Login Failure"), "ticket-login-failure");
}

#[test]
fn trims_outer_whitespace_before_slugifying() {
    assert_eq!(format_ticket_slug("  Cache Miss  "), "ticket-cache-miss");
}
"#;
            tokio::fs::write(dir.join("tests.rs"), tests_code)
                .await
                .map_err(|e| eyre!("Failed to write tests.rs: {}", e))?;
        }
        "tool_recovery_targeted_edit" => {
            let dir = temp_dir.join("tool_recovery_targeted_edit");
            tokio::fs::create_dir_all(&dir)
                .await
                .map_err(|e| eyre!("Failed to create dir {}: {}", dir.display(), e))?;
            tokio::fs::write(dir.join("timeouts.rs"), setup_code)
                .await
                .map_err(|e| eyre!("Failed to write timeouts.rs: {}", e))?;
            let tests_code = r#"mod timeouts;

use timeouts::{default_api_timeout_seconds, default_ui_timeout_seconds, timeout_profile};

#[test]
fn api_timeout_default_is_updated() {
    assert_eq!(default_api_timeout_seconds(), 30);
}

#[test]
fn ui_timeout_default_is_unchanged() {
    assert_eq!(default_ui_timeout_seconds(), 15);
}

#[test]
fn profile_reflects_mixed_defaults() {
    assert_eq!(timeout_profile(), (30, 15));
}
"#;
            tokio::fs::write(dir.join("tests.rs"), tests_code)
                .await
                .map_err(|e| eyre!("Failed to write tests.rs: {}", e))?;
        }
        "tool_recovery_verification_discipline" => {
            let dir = temp_dir.join("tool_recovery_verification_discipline");
            tokio::fs::create_dir_all(&dir)
                .await
                .map_err(|e| eyre!("Failed to create dir {}: {}", dir.display(), e))?;
            tokio::fs::write(dir.join("status.rs"), setup_code)
                .await
                .map_err(|e| eyre!("Failed to write status.rs: {}", e))?;
            let tests_code = r#"mod status;

use status::is_transition_allowed;

#[test]
fn allows_queued_to_running() {
    assert!(is_transition_allowed("queued", "running"));
}

#[test]
fn disallows_queued_to_completed() {
    assert!(!is_transition_allowed("queued", "completed"));
}

#[test]
fn disallows_completed_to_failed() {
    assert!(!is_transition_allowed("completed", "failed"));
}
"#;
            tokio::fs::write(dir.join("tests.rs"), tests_code)
                .await
                .map_err(|e| eyre!("Failed to write tests.rs: {}", e))?;
        }
        "prompt_regression_verify_before_finish" => {
            let dir = temp_dir.join("prompt_regression_verify_before_finish");
            tokio::fs::create_dir_all(&dir)
                .await
                .map_err(|e| eyre!("Failed to create dir {}: {}", dir.display(), e))?;
            tokio::fs::write(dir.join("health.rs"), setup_code)
                .await
                .map_err(|e| eyre!("Failed to write health.rs: {}", e))?;
            let tests_code = r#"mod health;

use health::is_healthy_status;

#[test]
fn allows_200_ok() {
    assert!(is_healthy_status(200));
}

#[test]
fn allows_299_redirect_boundary() {
    assert!(is_healthy_status(299));
}

#[test]
fn rejects_400_client_error() {
    assert!(!is_healthy_status(400));
}
"#;
            tokio::fs::write(dir.join("tests.rs"), tests_code)
                .await
                .map_err(|e| eyre!("Failed to write tests.rs: {}", e))?;
        }
        "prompt_regression_targeted_edit_only" => {
            let dir = temp_dir.join("prompt_regression_targeted_edit_only");
            tokio::fs::create_dir_all(&dir)
                .await
                .map_err(|e| eyre!("Failed to create dir {}: {}", dir.display(), e))?;
            tokio::fs::write(dir.join("endpoints.rs"), setup_code)
                .await
                .map_err(|e| eyre!("Failed to write endpoints.rs: {}", e))?;
            let tests_code = r#"mod endpoints;

use endpoints::{default_api_base_url, default_web_base_url};

#[test]
fn api_url_is_promoted_to_prod() {
    assert_eq!(default_api_base_url(), "https://api.prod.local");
}

#[test]
fn web_url_remains_unchanged() {
    assert_eq!(default_web_base_url(), "https://web.dev.local");
}
"#;
            tokio::fs::write(dir.join("tests.rs"), tests_code)
                .await
                .map_err(|e| eyre!("Failed to write tests.rs: {}", e))?;
        }
        "prompt_regression_minimal_patch" => {
            let dir = temp_dir.join("prompt_regression_minimal_patch");
            tokio::fs::create_dir_all(&dir)
                .await
                .map_err(|e| eyre!("Failed to create dir {}: {}", dir.display(), e))?;
            tokio::fs::write(dir.join("normalize.rs"), setup_code)
                .await
                .map_err(|e| eyre!("Failed to write normalize.rs: {}", e))?;
            let tests_code = r#"mod normalize;

use normalize::normalize_tag;

#[test]
fn lowercases_and_trims() {
    assert_eq!(normalize_tag("  Release Candidate  "), "release-candidate");
}

#[test]
fn collapses_internal_whitespace_sequences() {
    assert_eq!(normalize_tag("Release\t\tCandidate   One"), "release-candidate-one");
}
"#;
            tokio::fs::write(dir.join("tests.rs"), tests_code)
                .await
                .map_err(|e| eyre!("Failed to write tests.rs: {}", e))?;
        }
        "prompt_regression_read_before_edit" => {
            let dir = temp_dir.join("prompt_regression_read_before_edit");
            tokio::fs::create_dir_all(&dir)
                .await
                .map_err(|e| eyre!("Failed to create dir {}: {}", dir.display(), e))?;
            tokio::fs::write(dir.join("parser.rs"), setup_code)
                .await
                .map_err(|e| eyre!("Failed to write parser.rs: {}", e))?;
            let readme = "# Timeout parser fixture\n\nBehavior contract: trim surrounding whitespace before parsing timeout values.\n";
            tokio::fs::write(dir.join("README.md"), readme)
                .await
                .map_err(|e| eyre!("Failed to write README.md: {}", e))?;
            let tests_code = r#"mod parser;

use parser::parse_timeout_ms;

#[test]
fn parses_plain_value() {
    assert_eq!(parse_timeout_ms("2500"), Some(2500));
}

#[test]
fn trims_before_parsing() {
    assert_eq!(parse_timeout_ms(" 3000 "), Some(3000));
}
"#;
            tokio::fs::write(dir.join("tests.rs"), tests_code)
                .await
                .map_err(|e| eyre!("Failed to write tests.rs: {}", e))?;
        }
        "prompt_regression_wrong_first_edit_recovery" => {
            let dir = temp_dir.join("prompt_regression_wrong_first_edit_recovery");
            tokio::fs::create_dir_all(&dir)
                .await
                .map_err(|e| eyre!("Failed to create dir {}: {}", dir.display(), e))?;
            let cargo_toml = r#"[package]
name = "prompt_regression_wrong_first_edit_recovery"
version = "0.1.0"
edition = "2021"

[[test]]
name = "recovery"
path = "tests.rs"

[workspace]
"#;
            tokio::fs::write(dir.join("Cargo.toml"), cargo_toml)
                .await
                .map_err(|e| eyre!("Failed to write Cargo.toml: {}", e))?;
            tokio::fs::write(dir.join("arithmetic.rs"), setup_code)
                .await
                .map_err(|e| eyre!("Failed to write arithmetic.rs: {}", e))?;
            let tests_code = r#"mod arithmetic;

use arithmetic::multiply;

#[test]
fn multiplies_positive_numbers() {
    assert_eq!(multiply(6, 7), 42);
}

#[test]
fn multiplies_by_zero() {
    assert_eq!(multiply(0, 9), 0);
}
"#;
            tokio::fs::write(dir.join("tests.rs"), tests_code)
                .await
                .map_err(|e| eyre!("Failed to write tests.rs: {}", e))?;
        }
        "prompt_regression_tool_choice_discipline" => {
            let dir = temp_dir.join("prompt_regression_tool_choice_discipline");
            tokio::fs::create_dir_all(&dir)
                .await
                .map_err(|e| eyre!("Failed to create dir {}: {}", dir.display(), e))?;
            tokio::fs::write(dir.join("policy.rs"), setup_code)
                .await
                .map_err(|e| eyre!("Failed to write policy.rs: {}", e))?;
            let tests_code = r#"mod policy;

use policy::default_retry_policy;

#[test]
fn policy_is_balanced() {
    assert_eq!(default_retry_policy(), ("balanced", 3));
}
"#;
            tokio::fs::write(dir.join("tests.rs"), tests_code)
                .await
                .map_err(|e| eyre!("Failed to write tests.rs: {}", e))?;
        }
        "stress_coding_log_pipeline" => {
            let dir = temp_dir.join("stress_coding_log_pipeline");
            tokio::fs::create_dir_all(&dir)
                .await
                .map_err(|e| eyre!("Failed to create dir {}: {}", dir.display(), e))?;
            tokio::fs::write(dir.join("parser.rs"), setup_code)
                .await
                .map_err(|e| eyre!("Failed to write parser.rs: {}", e))?;
            let aggregates_code = r#"pub fn p95_latency(latencies: &[u64]) -> u64 {
    if latencies.is_empty() {
        return 0;
    }

    let mut sorted = latencies.to_vec();
    sorted.sort_unstable();
    let idx = (sorted.len() * 95) / 100;
    sorted[idx]
}

pub fn error_rate(levels: &[String]) -> f64 {
    if levels.is_empty() {
        return 0.0;
    }

    let errors = levels.iter().filter(|level| level.as_str() == "error").count();
    (errors / levels.len()) as f64
}
"#;
            let tests_code = r#"mod parser;
mod aggregates;

use aggregates::{error_rate, p95_latency};
use parser::parse_log_line;

#[test]
fn parser_trims_and_parses() {
    let parsed = parse_log_line(" warn | 120 ").unwrap();
    assert_eq!(parsed, ("warn".to_string(), 120));
}

#[test]
fn parser_rejects_malformed_lines() {
    assert!(parse_log_line("warn-only").is_none());
    assert!(parse_log_line("warn|abc").is_none());
}

#[test]
fn p95_uses_last_valid_index() {
    let values = vec![10, 20, 30, 40, 50, 60, 70, 80, 90, 100];
    assert_eq!(p95_latency(&values), 100);
}

#[test]
fn error_rate_returns_fraction() {
    let levels = vec![
        "info".to_string(),
        "error".to_string(),
        "warn".to_string(),
        "error".to_string(),
    ];
    assert!((error_rate(&levels) - 0.5).abs() < 1e-9);
}
"#;
            tokio::fs::write(dir.join("aggregates.rs"), aggregates_code)
                .await
                .map_err(|e| eyre!("Failed to write aggregates.rs: {}", e))?;
            tokio::fs::write(dir.join("tests.rs"), tests_code)
                .await
                .map_err(|e| eyre!("Failed to write tests.rs: {}", e))?;
        }
        "large_project_feature_flags" => {
            let dir = temp_dir.join("large_project_feature_flags");
            tokio::fs::create_dir_all(&dir)
                .await
                .map_err(|e| eyre!("Failed to create dir {}: {}", dir.display(), e))?;
            tokio::fs::write(dir.join("evaluator.rs"), setup_code)
                .await
                .map_err(|e| eyre!("Failed to write evaluator.rs: {}", e))?;
            let rules_code = r#"#[derive(Debug, Clone)]
pub struct FeatureRule {
    pub name: String,
    pub enabled: bool,
    pub rollout_percent: u8,
}

impl FeatureRule {
    pub fn new(name: &str, enabled: bool, rollout_percent: u8) -> Self {
        Self {
            name: name.to_string(),
            enabled,
            rollout_percent,
        }
    }
}
"#;
            let rollout_code = r#"pub fn in_rollout(user_id: u64, percent: u8) -> bool {
    if percent == 0 {
        return false;
    }
    if percent >= 100 {
        return true;
    }

    (user_id % 100) <= percent as u64
}
"#;
            let tests_code = r#"mod rules;
mod rollout;
mod evaluator;

use evaluator::is_feature_enabled;
use rules::FeatureRule;

#[test]
fn disabled_flag_stays_disabled_for_normal_users() {
    let rule = FeatureRule::new("beta_dashboard", false, 100);
    assert!(!is_feature_enabled(&rule, 17, &[]));
}

#[test]
fn allowlist_overrides_disabled_flag() {
    let rule = FeatureRule::new("beta_dashboard", false, 0);
    assert!(is_feature_enabled(&rule, 42, &[42]));
}

#[test]
fn zero_percent_never_rolls_out() {
    let rule = FeatureRule::new("beta_dashboard", true, 0);
    assert!(!is_feature_enabled(&rule, 10, &[]));
}

#[test]
fn hundred_percent_always_rolls_out() {
    let rule = FeatureRule::new("beta_dashboard", true, 100);
    assert!(is_feature_enabled(&rule, 99, &[]));
}

#[test]
fn rollout_uses_strict_upper_bound() {
    let rule = FeatureRule::new("beta_dashboard", true, 25);
    assert!(is_feature_enabled(&rule, 24, &[]));
    assert!(!is_feature_enabled(&rule, 25, &[]));
}
"#;
            tokio::fs::write(dir.join("rules.rs"), rules_code)
                .await
                .map_err(|e| eyre!("Failed to write rules.rs: {}", e))?;
            tokio::fs::write(dir.join("rollout.rs"), rollout_code)
                .await
                .map_err(|e| eyre!("Failed to write rollout.rs: {}", e))?;
            tokio::fs::write(dir.join("tests.rs"), tests_code)
                .await
                .map_err(|e| eyre!("Failed to write tests.rs: {}", e))?;
        }
        "test_heavy_csv_regressions" => {
            let dir = temp_dir.join("test_heavy_csv_regressions");
            tokio::fs::create_dir_all(&dir)
                .await
                .map_err(|e| eyre!("Failed to create dir {}: {}", dir.display(), e))?;
            tokio::fs::write(dir.join("parser.rs"), setup_code)
                .await
                .map_err(|e| eyre!("Failed to write parser.rs: {}", e))?;
            let tests_code = r#"mod parser;

use parser::{parse_record, parse_rows};

#[test]
fn parses_basic_row() {
    assert_eq!(parse_record("a,b"), vec!["a".to_string(), "b".to_string()]);
}

#[test]
fn parses_multiple_rows() {
    let rows = parse_rows("x,y\n1,2\n");
    assert_eq!(rows.len(), 2);
}
"#;
            tokio::fs::write(dir.join("tests.rs"), tests_code)
                .await
                .map_err(|e| eyre!("Failed to write tests.rs: {}", e))?;
        }
        "maintenance_config_migration" => {
            let dir = temp_dir.join("maintenance_config_migration");
            tokio::fs::create_dir_all(&dir)
                .await
                .map_err(|e| eyre!("Failed to create dir {}: {}", dir.display(), e))?;
            tokio::fs::write(dir.join("config.rs"), setup_code)
                .await
                .map_err(|e| eyre!("Failed to write config.rs: {}", e))?;
            let migrate_code = r#"use crate::config::ServiceConfig;

pub fn parse_legacy_config(input: &str) -> Result<ServiceConfig, String> {
    let mut timeout_ms: u64 = 0;
    let mut retry_limit: u8 = 0;

    for line in input.lines() {
        let Some((key, value)) = line.split_once('=') else {
            continue;
        };
        let key = key.trim();
        let value = value.trim();

        match key {
            "timeout_ms" => {
                timeout_ms = value.parse().map_err(|_| "invalid timeout_ms".to_string())?;
            }
            "retry_limit" => {
                retry_limit = value.parse().map_err(|_| "invalid retry_limit".to_string())?;
            }
            _ => {}
        }
    }

    Ok(ServiceConfig::from_legacy_timeout_ms(timeout_ms, retry_limit))
}
"#;
            let render_code = r#"use crate::config::ServiceConfig;

pub fn render_summary(config: &ServiceConfig) -> String {
    format!(
        "timeout={}s retry_limit={}",
        config.timeout_seconds, config.retry_limit
    )
}
"#;
            let tests_code = r#"mod config;
mod migrate;
mod render;
use config::ServiceConfig;
use migrate::parse_legacy_config;
use render::render_summary;

#[test]
fn conversion_keeps_minimum_one_second_for_non_zero_timeout() {
    let cfg = ServiceConfig::from_legacy_timeout_ms(1, 3);
    assert_eq!(cfg.timeout_seconds, 1);
}

#[test]
fn parse_legacy_supports_timeout_ms() {
    let cfg = parse_legacy_config("timeout_ms=2500\nretry_limit=4").unwrap();
    assert_eq!(cfg.timeout_seconds, 3);
    assert_eq!(cfg.retry_limit, 4);
}

#[test]
fn render_uses_seconds_label() {
    let cfg = ServiceConfig {
        timeout_seconds: 3,
        retry_limit: 2,
    };
    assert_eq!(render_summary(&cfg), "timeout=3s retry_limit=2");
}
"#;
            tokio::fs::write(dir.join("migrate.rs"), migrate_code)
                .await
                .map_err(|e| eyre!("Failed to write migrate.rs: {}", e))?;
            tokio::fs::write(dir.join("render.rs"), render_code)
                .await
                .map_err(|e| eyre!("Failed to write render.rs: {}", e))?;
            tokio::fs::write(dir.join("tests.rs"), tests_code)
                .await
                .map_err(|e| eyre!("Failed to write tests.rs: {}", e))?;
        }
        _ => return Ok(()),
    }

    Ok(())
}
