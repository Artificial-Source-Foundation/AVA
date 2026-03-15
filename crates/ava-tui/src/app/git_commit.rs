use crate::state::messages::MessageKind;
use std::collections::BTreeSet;

pub(super) fn handle_commit_command() -> (MessageKind, String) {
    if !git_command_succeeds(["rev-parse", "--is-inside-work-tree"]) {
        return (
            MessageKind::Error,
            "`/commit` only works inside a git repository.".to_string(),
        );
    }

    let branch = git_stdout(["branch", "--show-current"]).unwrap_or_else(|| "unknown".to_string());
    let Some(status_text) = git_stdout(["status", "--short"]) else {
        return (
            MessageKind::Error,
            "Failed to run `git status --short`.".to_string(),
        );
    };

    let status = CommitPrepStatus::from_porcelain(&status_text);
    if status.entries.is_empty() {
        return (
            MessageKind::System,
            format!("Branch `{branch}` is clean. Nothing to commit."),
        );
    }

    let staged_diff = git_stdout(["diff", "--cached", "--stat"])
        .unwrap_or_else(|| "(unable to read staged diff stat)".to_string());
    let unstaged_diff = git_stdout(["diff", "--stat"])
        .unwrap_or_else(|| "(unable to read unstaged diff stat)".to_string());
    let recent_log = git_stdout(["log", "-5", "--pretty=format:%s"]).unwrap_or_default();
    let suggestion = status.suggested_message();

    let mut lines = vec![
        format!("Branch: `{branch}`"),
        format!(
            "Commit prep: {} staged, {} unstaged, {} untracked",
            status.staged_count, status.unstaged_count, status.untracked_count
        ),
    ];

    if !status.staged_paths.is_empty() {
        lines.push(String::new());
        lines.push("Staged files:".to_string());
        lines.extend(status.staged_paths.iter().map(|path| format!("  {path}")));
        if !staged_diff.trim().is_empty() {
            lines.push("Staged diff stat:".to_string());
            lines.extend(staged_diff.lines().map(|line| format!("  {line}")));
        }
    }

    if status.unstaged_count > 0 || status.untracked_count > 0 {
        lines.push(String::new());
        lines.push("Not yet staged:".to_string());
        lines.extend(status.unstaged_paths.iter().map(|path| format!("  {path}")));
        lines.extend(
            status
                .untracked_paths
                .iter()
                .map(|path| format!("  {path}")),
        );
        if !unstaged_diff.trim().is_empty() {
            lines.push("Unstaged diff stat:".to_string());
            lines.extend(unstaged_diff.lines().map(|line| format!("  {line}")));
        }
    }

    lines.push(String::new());
    lines.push(format!("Suggested commit message: `{suggestion}`"));

    if !recent_log.trim().is_empty() {
        lines.push("Recent commit style:".to_string());
        lines.extend(recent_log.lines().map(|line| format!("  - {line}")));
    }

    lines.push(String::new());
    if status.staged_count == 0 {
        lines.push(
            "No staged changes yet. Stage the files you want included, then ask AVA to commit with the suggested message or a refined variant.".to_string(),
        );
    } else if status.unstaged_count > 0 || status.untracked_count > 0 {
        lines.push(
            "Only staged changes are commit-ready. Unstaged and untracked files will be excluded until you add them.".to_string(),
        );
        lines.push(
            "AVA will not auto-commit from `/commit`; review the suggestion, then explicitly ask it to create the commit if you want that next step.".to_string(),
        );
    } else {
        lines.push(
            "These staged changes look commit-ready. AVA will not auto-commit from `/commit`; explicitly ask it to create the commit when ready.".to_string(),
        );
    }

    (MessageKind::System, lines.join("\n"))
}

fn git_stdout<const N: usize>(args: [&str; N]) -> Option<String> {
    let output = std::process::Command::new("git").args(args).output().ok()?;
    if !output.status.success() {
        return None;
    }
    Some(String::from_utf8_lossy(&output.stdout).trim().to_string())
}

fn git_command_succeeds<const N: usize>(args: [&str; N]) -> bool {
    std::process::Command::new("git")
        .args(args)
        .output()
        .map(|output| output.status.success())
        .unwrap_or(false)
}

#[derive(Debug, Clone, PartialEq, Eq)]
struct CommitPrepStatus {
    entries: Vec<GitStatusEntry>,
    staged_count: usize,
    unstaged_count: usize,
    untracked_count: usize,
    staged_paths: Vec<String>,
    unstaged_paths: Vec<String>,
    untracked_paths: Vec<String>,
}

impl CommitPrepStatus {
    fn from_porcelain(input: &str) -> Self {
        let entries: Vec<GitStatusEntry> =
            input.lines().filter_map(GitStatusEntry::parse).collect();

        let mut staged_paths = BTreeSet::new();
        let mut unstaged_paths = BTreeSet::new();
        let mut untracked_paths = BTreeSet::new();
        let mut staged_count = 0;
        let mut unstaged_count = 0;
        let mut untracked_count = 0;

        for entry in &entries {
            if entry.is_untracked() {
                untracked_count += 1;
                untracked_paths.insert(entry.path.clone());
            } else {
                if entry.is_staged() {
                    staged_count += 1;
                    staged_paths.insert(entry.path.clone());
                }
                if entry.is_unstaged() {
                    unstaged_count += 1;
                    unstaged_paths.insert(entry.path.clone());
                }
            }
        }

        Self {
            entries,
            staged_count,
            unstaged_count,
            untracked_count,
            staged_paths: staged_paths.into_iter().collect(),
            unstaged_paths: unstaged_paths.into_iter().collect(),
            untracked_paths: untracked_paths.into_iter().collect(),
        }
    }

    fn suggested_message(&self) -> String {
        let relevant: Vec<&GitStatusEntry> = if self.staged_count > 0 {
            self.entries
                .iter()
                .filter(|entry| entry.is_staged())
                .collect()
        } else {
            self.entries.iter().collect()
        };

        let action = if !relevant.is_empty()
            && relevant.iter().all(|entry| entry.is_addition_like())
        {
            "add"
        } else if !relevant.is_empty() && relevant.iter().all(|entry| entry.is_deletion_like()) {
            "remove"
        } else if relevant.iter().any(|entry| entry.is_rename_like()) {
            "rename"
        } else {
            "update"
        };

        let scope = suggest_scope(&relevant);
        format!("{action} {scope}")
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
struct GitStatusEntry {
    staged: char,
    unstaged: char,
    path: String,
}

impl GitStatusEntry {
    fn parse(line: &str) -> Option<Self> {
        let bytes = line.as_bytes();
        if bytes.len() < 4 {
            return None;
        }

        Some(Self {
            staged: bytes[0] as char,
            unstaged: bytes[1] as char,
            path: line[3..].trim().to_string(),
        })
    }

    fn is_untracked(&self) -> bool {
        self.staged == '?' && self.unstaged == '?'
    }

    fn is_staged(&self) -> bool {
        !self.is_untracked() && self.staged != ' '
    }

    fn is_unstaged(&self) -> bool {
        !self.is_untracked() && self.unstaged != ' '
    }

    fn is_addition_like(&self) -> bool {
        self.is_untracked() || self.staged == 'A'
    }

    fn is_deletion_like(&self) -> bool {
        self.staged == 'D' || self.unstaged == 'D'
    }

    fn is_rename_like(&self) -> bool {
        self.staged == 'R' || self.unstaged == 'R'
    }
}

fn suggest_scope(entries: &[&GitStatusEntry]) -> String {
    if entries.is_empty() {
        return "changes".to_string();
    }

    let normalized_paths: Vec<String> = entries
        .iter()
        .map(|entry| normalize_status_path(&entry.path))
        .collect();

    if normalized_paths.len() == 1 {
        return normalized_paths[0].clone();
    }

    let top_dirs: BTreeSet<String> = normalized_paths
        .iter()
        .filter_map(|path| path.split('/').next().map(str::to_string))
        .collect();

    if top_dirs.len() == 1 {
        return top_dirs
            .into_iter()
            .next()
            .unwrap_or_else(|| "changes".to_string());
    }

    "changes".to_string()
}

fn normalize_status_path(path: &str) -> String {
    if let Some((_, renamed_to)) = path.split_once(" -> ") {
        renamed_to.trim().to_string()
    } else {
        path.trim().to_string()
    }
}

#[cfg(test)]
mod tests {
    use super::{suggest_scope, CommitPrepStatus, GitStatusEntry};

    #[test]
    fn commit_prep_counts_status_groups() {
        let status = CommitPrepStatus::from_porcelain(
            "M  crates/ava-tui/src/app/commands.rs\n M crates/ava-agent/src/lib.rs\n?? docs/note.md\n",
        );

        assert_eq!(status.staged_count, 1);
        assert_eq!(status.unstaged_count, 1);
        assert_eq!(status.untracked_count, 1);
        assert_eq!(
            status.staged_paths,
            vec!["crates/ava-tui/src/app/commands.rs"]
        );
        assert_eq!(status.unstaged_paths, vec!["crates/ava-agent/src/lib.rs"]);
        assert_eq!(status.untracked_paths, vec!["docs/note.md"]);
    }

    #[test]
    fn commit_prep_suggests_add_for_new_files() {
        let status = CommitPrepStatus::from_porcelain(
            "A  crates/ava-tools/src/git/snapshot.rs\n?? crates/ava-tools/src/edit/strategies/relative_indent.rs\n",
        );
        assert_eq!(
            status.suggested_message(),
            "add crates/ava-tools/src/git/snapshot.rs"
        );
    }

    #[test]
    fn commit_prep_suggests_single_file_scope() {
        let entry = GitStatusEntry::parse("M  crates/ava-tui/src/app/commands.rs").unwrap();
        assert_eq!(
            suggest_scope(&[&entry]),
            "crates/ava-tui/src/app/commands.rs"
        );
    }

    #[test]
    fn commit_prep_uses_rename_destination_for_scope() {
        let entry = GitStatusEntry::parse("R  old/name.rs -> new/name.rs").unwrap();
        assert_eq!(suggest_scope(&[&entry]), "new/name.rs");
    }
}
