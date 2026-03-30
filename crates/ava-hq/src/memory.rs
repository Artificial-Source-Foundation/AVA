use std::path::{Path, PathBuf};

use ava_types::{AvaError, Result};
use serde::{Deserialize, Serialize};
use tokio::fs;

#[derive(Debug, Clone, Serialize, Deserialize, Default)]
#[serde(rename_all = "camelCase")]
pub struct HqMemoryBootstrapOptions {
    #[serde(default)]
    pub director_model: Option<String>,
    #[serde(default)]
    pub force: bool,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct HqMemoryBootstrapResult {
    pub project_root: String,
    pub hq_root: String,
    pub project_name: String,
    pub stack_summary: Vec<String>,
    pub created_files: Vec<String>,
    pub reused_existing: bool,
}

struct ProjectScan {
    project_name: String,
    stack_summary: Vec<String>,
    important_paths: Vec<String>,
}

pub async fn bootstrap_hq_memory(
    project_root: &Path,
    options: &HqMemoryBootstrapOptions,
) -> Result<HqMemoryBootstrapResult> {
    let scan = scan_project(project_root).await?;
    let hq_root = project_root.join(".ava").join("HQ");
    let front_page = hq_root.join("FRONT_PAGE.md");

    let reused_existing = front_page.exists() && !options.force;
    if reused_existing {
        return Ok(HqMemoryBootstrapResult {
            project_root: project_root.display().to_string(),
            hq_root: hq_root.display().to_string(),
            project_name: scan.project_name,
            stack_summary: scan.stack_summary,
            created_files: vec![],
            reused_existing: true,
        });
    }

    fs::create_dir_all(hq_root.join("DESK"))
        .await
        .map_err(|error| AvaError::IoError(error.to_string()))?;
    fs::create_dir_all(hq_root.join("CABINET").join("decisions"))
        .await
        .map_err(|error| AvaError::IoError(error.to_string()))?;
    fs::create_dir_all(hq_root.join("CABINET").join("summaries"))
        .await
        .map_err(|error| AvaError::IoError(error.to_string()))?;
    fs::create_dir_all(hq_root.join("CABINET").join("archive"))
        .await
        .map_err(|error| AvaError::IoError(error.to_string()))?;
    fs::create_dir_all(project_root.join(".ava"))
        .await
        .map_err(|error| AvaError::IoError(error.to_string()))?;

    let important_paths = format_bullets(&scan.important_paths);
    let stack_summary = format_bullets(&scan.stack_summary);
    let director_line = options
        .director_model
        .as_deref()
        .filter(|value| !value.trim().is_empty())
        .map(|value| format!("- Preferred Director model: `{value}`\n"))
        .unwrap_or_default();

    let files = vec![
        (
            project_root.join(".ava").join(".gitignore"),
            "HQ/\n".to_string(),
        ),
        (
            hq_root.join("FRONT_PAGE.md"),
            format!(
                "# HQ Front Page\n\n## Purpose\n- Tiny always-read orientation for the Director.\n\n## Project Snapshot\n- Project: `{}`\n- Root: `{}`\n{}\n## Detected stack\n{}\n## Important repo paths\n{}\n## Operating rules\n- Treat this file as stable truth unless the user confirms a major change.\n- Use `DESK/` for active work and `CABINET/` for durable memory.\n- Promote shared truths into normal project docs only when the user wants that.\n\n## Read next\n- `MANIFEST.md`\n- `DESK/current-status.md`\n- `DESK/handoff.md`\n",
                scan.project_name,
                project_root.display(),
                director_line,
                stack_summary,
                important_paths,
            ),
        ),
        (
            hq_root.join("MANIFEST.md"),
            "# HQ Manifest\n\n## Purpose\n- Routing guide for the Director's private notebook.\n\n## Read order\n1. `FRONT_PAGE.md`\n2. `DESK/current-status.md`\n3. `DESK/handoff.md`\n4. `DESK/backlog.md`\n5. `CABINET/index.md` only when more context is needed\n\n## Update rules\n- Auto-update: `DESK/current-status.md`, `DESK/backlog.md`, `DESK/handoff.md`, session summaries.\n- Confirm with user: important project truths, conventions, architecture assumptions.\n- Hard approval: HQ operating policy and budget rules.\n\n## Folder roles\n- `DESK/` = active notebook\n- `CABINET/decisions/` = durable confirmed decisions\n- `CABINET/summaries/` = compacted session memory\n- `CABINET/archive/` = old memory kept for reference\n".to_string(),
        ),
        (
            hq_root.join("DESK").join("current-status.md"),
            "# Current Status\n\n## State\n- HQ memory initialized.\n- No active execution yet.\n\n## Next suggested actions\n- Confirm the project snapshot in `FRONT_PAGE.md`.\n- Capture the first active initiative in `backlog.md`.\n- Leave a short note in `handoff.md` when a session ends.\n".to_string(),
        ),
        (
            hq_root.join("DESK").join("backlog.md"),
            "# Backlog\n\n## Near-term\n- Confirm important project facts inferred during HQ setup.\n- Add the first major initiative the Director should track.\n- Promote any stable shared truths into `docs/` only when user-approved.\n".to_string(),
        ),
        (
            hq_root.join("DESK").join("handoff.md"),
            "# Handoff\n\n## For future Director\n- HQ was just initialized for this project.\n- Start by reading `FRONT_PAGE.md`, then confirm current priorities with the user.\n".to_string(),
        ),
        (
            hq_root.join("DESK").join("proposed-updates.md"),
            "# Proposed Updates\n\nUse this file for important truths that HQ believes may have changed but should not silently rewrite.\n\n## Pending confirmations\n- None yet.\n".to_string(),
        ),
        (
            hq_root.join("CABINET").join("index.md"),
            "# Cabinet Index\n\n## Purpose\n- Quick map of deeper HQ memory. Read this before opening older files.\n\n## Sections\n- `decisions/` -- confirmed durable decisions\n- `summaries/` -- compacted session/phase notes\n- `archive/` -- older private memory\n\n## Current state\n- Decisions: 0\n- Summaries: 0\n- Archive entries: 0\n".to_string(),
        ),
        (
            hq_root.join("CABINET").join("decisions").join("README.md"),
            "# Decisions\n\nStore only important confirmed decisions here. Prefer concise ADR-style notes over long transcripts.\n".to_string(),
        ),
        (
            hq_root.join("CABINET").join("summaries").join("README.md"),
            "# Summaries\n\nStore compressed session or phase summaries here. Each summary should answer: what changed, where we stand, what is next, and what still needs confirmation.\n".to_string(),
        ),
        (
            hq_root.join("CABINET").join("archive").join("README.md"),
            "# Archive\n\nOlder private HQ memory that should still exist but does not need to stay top-of-mind.\n".to_string(),
        ),
    ];

    let mut created_files = Vec::with_capacity(files.len());
    for (path, content) in files {
        write_file(path.as_path(), &content).await?;
        created_files.push(path.display().to_string());
    }

    Ok(HqMemoryBootstrapResult {
        project_root: project_root.display().to_string(),
        hq_root: hq_root.display().to_string(),
        project_name: scan.project_name,
        stack_summary: scan.stack_summary,
        created_files,
        reused_existing: false,
    })
}

async fn write_file(path: &Path, content: &str) -> Result<()> {
    if let Some(parent) = path.parent() {
        fs::create_dir_all(parent)
            .await
            .map_err(|error| AvaError::IoError(error.to_string()))?;
    }
    fs::write(path, content)
        .await
        .map_err(|error| AvaError::IoError(error.to_string()))
}

async fn scan_project(project_root: &Path) -> Result<ProjectScan> {
    let project_name = project_root
        .file_name()
        .map(|value| value.to_string_lossy().to_string())
        .filter(|value| !value.is_empty())
        .unwrap_or_else(|| "project".to_string());

    let package_json = read_optional_text(project_root.join("package.json")).await;

    let mut stack_summary = Vec::new();
    if project_root.join("Cargo.toml").exists() {
        stack_summary.push("Rust workspace or crate".to_string());
    }
    if project_root.join("src-tauri").join("Cargo.toml").exists() {
        stack_summary.push("Tauri desktop app".to_string());
    }
    if package_json
        .as_deref()
        .is_some_and(|content| content.contains("solid-js"))
    {
        stack_summary.push("SolidJS frontend".to_string());
    }
    if project_root.join("tsconfig.json").exists()
        || package_json
            .as_deref()
            .is_some_and(|content| content.contains("typescript"))
    {
        stack_summary.push("TypeScript frontend/tooling".to_string());
    }
    if project_root.join("pnpm-lock.yaml").exists() {
        stack_summary.push("pnpm workspace/dependencies".to_string());
    }
    if project_root.join("AGENTS.md").exists() {
        stack_summary.push("Repo-scoped AI instructions via `AGENTS.md`".to_string());
    }
    if project_root.join("CLAUDE.md").exists() {
        stack_summary.push("Architecture/project notes via `CLAUDE.md`".to_string());
    }
    if stack_summary.is_empty() {
        stack_summary.push("Project stack still needs confirmation".to_string());
    }

    let important_candidates = [
        "AGENTS.md",
        "CLAUDE.md",
        "Cargo.toml",
        "package.json",
        "src-tauri/Cargo.toml",
        "docs/README.md",
        "docs/backlog.md",
        "CHANGELOG.md",
    ];
    let important_paths = important_candidates
        .iter()
        .filter(|relative| project_root.join(relative).exists())
        .map(|relative| (*relative).to_string())
        .collect();

    Ok(ProjectScan {
        project_name,
        stack_summary,
        important_paths,
    })
}

async fn read_optional_text(path: PathBuf) -> Option<String> {
    fs::read_to_string(path).await.ok()
}

fn format_bullets(items: &[String]) -> String {
    if items.is_empty() {
        return "- None yet\n".to_string();
    }
    let mut out = String::new();
    for item in items {
        out.push_str("- ");
        out.push_str(item);
        out.push('\n');
    }
    out
}

#[cfg(test)]
mod tests {
    use super::*;

    #[tokio::test]
    async fn bootstrap_creates_hq_memory_files() {
        let tempdir = tempfile::tempdir().expect("tempdir");
        fs::write(
            tempdir.path().join("Cargo.toml"),
            "[package]\nname='demo'\nversion='0.1.0'\n",
        )
        .await
        .expect("write Cargo.toml");
        fs::write(
            tempdir.path().join("AGENTS.md"),
            "Use Rust for backend work.\n",
        )
        .await
        .expect("write AGENTS.md");

        let result = bootstrap_hq_memory(tempdir.path(), &HqMemoryBootstrapOptions::default())
            .await
            .expect("bootstrap should succeed");

        assert!(!result.reused_existing);
        assert!(tempdir.path().join(".ava/HQ/FRONT_PAGE.md").exists());
        assert!(tempdir.path().join(".ava/HQ/MANIFEST.md").exists());
        assert!(tempdir.path().join(".ava/.gitignore").exists());
    }
}
