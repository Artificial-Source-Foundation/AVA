//! Git diff stat tracking per session.
//!
//! Computes the number of files changed, lines added, and lines deleted
//! since a given git ref (or unstaged, by default) by parsing `git diff --numstat`.

use serde::{Deserialize, Serialize};

/// Accumulated diff statistics for a session.
#[derive(Debug, Default, Clone, Serialize, Deserialize, PartialEq, Eq)]
pub struct SessionDiffStats {
    pub files_changed: usize,
    pub additions: usize,
    pub deletions: usize,
}

impl std::fmt::Display for SessionDiffStats {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(
            f,
            "{} file{} changed, {} insertion{}, {} deletion{}",
            self.files_changed,
            if self.files_changed == 1 { "" } else { "s" },
            self.additions,
            if self.additions == 1 { "(+)" } else { "s(+)" },
            self.deletions,
            if self.deletions == 1 { "(-)" } else { "s(-)" },
        )
    }
}

/// Compute diff stats for the current working tree against HEAD.
///
/// This captures both staged and unstaged changes (`git diff HEAD --numstat`).
/// Returns `None` if git is not available or the directory is not a git repo.
pub fn compute_session_diff() -> Option<SessionDiffStats> {
    compute_diff_against("HEAD")
}

/// Compute diff stats against a specific git ref (commit, branch, tag).
pub fn compute_diff_against(git_ref: &str) -> Option<SessionDiffStats> {
    let output = std::process::Command::new("git")
        .args(["diff", git_ref, "--numstat"])
        .output()
        .ok()?;

    if !output.status.success() {
        return None;
    }

    let stdout = String::from_utf8_lossy(&output.stdout);
    Some(parse_numstat(&stdout))
}

/// Compute diff stats for staged changes only (`git diff --cached --numstat`).
pub fn compute_staged_diff() -> Option<SessionDiffStats> {
    let output = std::process::Command::new("git")
        .args(["diff", "--cached", "--numstat"])
        .output()
        .ok()?;

    if !output.status.success() {
        return None;
    }

    let stdout = String::from_utf8_lossy(&output.stdout);
    Some(parse_numstat(&stdout))
}

/// Parse `git diff --numstat` output.
///
/// Each line is: `<additions>\t<deletions>\t<filename>`
/// Binary files show `-\t-\t<filename>` — we skip those.
fn parse_numstat(output: &str) -> SessionDiffStats {
    let mut stats = SessionDiffStats::default();

    for line in output.lines() {
        let line = line.trim();
        if line.is_empty() {
            continue;
        }

        let parts: Vec<&str> = line.splitn(3, '\t').collect();
        if parts.len() < 3 {
            continue;
        }

        // Binary files report "-" for additions/deletions — count the file but skip numbers
        let additions = parts[0].parse::<usize>().unwrap_or(0);
        let deletions = parts[1].parse::<usize>().unwrap_or(0);

        stats.files_changed += 1;
        stats.additions += additions;
        stats.deletions += deletions;
    }

    stats
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parse_numstat_basic() {
        let input = "10\t5\tsrc/main.rs\n3\t0\tsrc/lib.rs\n";
        let stats = parse_numstat(input);
        assert_eq!(stats.files_changed, 2);
        assert_eq!(stats.additions, 13);
        assert_eq!(stats.deletions, 5);
    }

    #[test]
    fn parse_numstat_empty() {
        let stats = parse_numstat("");
        assert_eq!(stats, SessionDiffStats::default());
    }

    #[test]
    fn parse_numstat_binary_files() {
        let input = "-\t-\timage.png\n5\t2\tsrc/lib.rs\n";
        let stats = parse_numstat(input);
        assert_eq!(stats.files_changed, 2);
        assert_eq!(stats.additions, 5);
        assert_eq!(stats.deletions, 2);
    }

    #[test]
    fn display_singular() {
        let stats = SessionDiffStats {
            files_changed: 1,
            additions: 1,
            deletions: 1,
        };
        let s = stats.to_string();
        assert!(s.contains("1 file changed"));
        assert!(s.contains("1 insertion(+)"));
        assert!(s.contains("1 deletion(-)"));
    }

    #[test]
    fn display_plural() {
        let stats = SessionDiffStats {
            files_changed: 3,
            additions: 10,
            deletions: 5,
        };
        let s = stats.to_string();
        assert!(s.contains("3 files changed"));
        assert!(s.contains("10 insertions(+)"));
        assert!(s.contains("5 deletions(-)"));
    }
}
