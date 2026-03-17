//! GitHub issue resolver (BG2-32, inspired by OpenHands).
//!
//! Parses GitHub issue URLs, fetches issue details via `gh` CLI,
//! and generates a structured goal message for the agent.

use std::process::Command;

/// Parsed GitHub issue reference.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct IssueRef {
    pub owner: String,
    pub repo: String,
    pub number: u64,
}

/// Fetched issue details.
#[derive(Debug, Clone)]
pub struct IssueDetails {
    pub reference: IssueRef,
    pub title: String,
    pub body: String,
    pub labels: Vec<String>,
    pub state: String,
    pub comments_count: usize,
}

/// Parse a GitHub issue URL into an IssueRef.
///
/// Supports formats:
/// - `https://github.com/owner/repo/issues/123`
/// - `owner/repo#123`
/// - `#123` (requires repo context)
pub fn parse_issue_ref(input: &str) -> Option<IssueRef> {
    let input = input.trim();

    // Full URL: https://github.com/owner/repo/issues/123
    if let Some(rest) = input
        .strip_prefix("https://github.com/")
        .or_else(|| input.strip_prefix("http://github.com/"))
    {
        let parts: Vec<&str> = rest.splitn(4, '/').collect();
        if parts.len() >= 4 && parts[2] == "issues" {
            let number = parts[3].split(&['?', '#'][..]).next()?.parse().ok()?;
            return Some(IssueRef {
                owner: parts[0].to_string(),
                repo: parts[1].to_string(),
                number,
            });
        }
        return None;
    }

    // Short format: owner/repo#123
    if let Some(hash_pos) = input.rfind('#') {
        let prefix = &input[..hash_pos];
        let number_str = &input[hash_pos + 1..];
        let number: u64 = number_str.parse().ok()?;

        if let Some(slash_pos) = prefix.find('/') {
            let owner = &prefix[..slash_pos];
            let repo = &prefix[slash_pos + 1..];
            if !owner.is_empty() && !repo.is_empty() {
                return Some(IssueRef {
                    owner: owner.to_string(),
                    repo: repo.to_string(),
                    number,
                });
            }
        }
    }

    None
}

/// Fetch issue details using the `gh` CLI.
pub fn fetch_issue(issue: &IssueRef) -> Result<IssueDetails, String> {
    let repo = format!("{}/{}", issue.owner, issue.repo);
    let output = Command::new("gh")
        .args([
            "issue",
            "view",
            &issue.number.to_string(),
            "--repo",
            &repo,
            "--json",
            "title,body,labels,state,comments",
        ])
        .output()
        .map_err(|e| format!("Failed to run gh CLI: {e}"))?;

    if !output.status.success() {
        let stderr = String::from_utf8_lossy(&output.stderr);
        return Err(format!("gh issue view failed: {stderr}"));
    }

    let json: serde_json::Value = serde_json::from_slice(&output.stdout)
        .map_err(|e| format!("Failed to parse gh output: {e}"))?;

    let title = json["title"].as_str().unwrap_or("").to_string();
    let body = json["body"].as_str().unwrap_or("").to_string();
    let state = json["state"].as_str().unwrap_or("open").to_string();
    let labels = json["labels"]
        .as_array()
        .map(|arr| {
            arr.iter()
                .filter_map(|l| l["name"].as_str().map(String::from))
                .collect()
        })
        .unwrap_or_default();
    let comments_count = json["comments"]
        .as_array()
        .map(|arr| arr.len())
        .unwrap_or(0);

    Ok(IssueDetails {
        reference: issue.clone(),
        title,
        body,
        labels,
        state,
        comments_count,
    })
}

/// Generate a structured goal message from issue details.
pub fn issue_to_goal(details: &IssueDetails) -> String {
    let mut goal = format!(
        "Resolve GitHub issue {}/{}#{}: {}\n\n",
        details.reference.owner, details.reference.repo, details.reference.number, details.title,
    );

    if !details.body.is_empty() {
        // Truncate very long issue bodies
        let body = if details.body.len() > 3000 {
            format!("{}...\n[body truncated]", &details.body[..3000])
        } else {
            details.body.clone()
        };
        goal.push_str("Issue description:\n");
        goal.push_str(&body);
        goal.push('\n');
    }

    if !details.labels.is_empty() {
        goal.push_str(&format!("\nLabels: {}\n", details.labels.join(", ")));
    }

    if details.comments_count > 0 {
        goal.push_str(&format!(
            "({} comments on the issue)\n",
            details.comments_count
        ));
    }

    goal.push_str(
        "\nPlease analyze the issue, find the relevant code, implement a fix, and verify it works.",
    );

    goal
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parse_full_url() {
        let issue = parse_issue_ref("https://github.com/owner/repo/issues/42").unwrap();
        assert_eq!(issue.owner, "owner");
        assert_eq!(issue.repo, "repo");
        assert_eq!(issue.number, 42);
    }

    #[test]
    fn parse_url_with_query() {
        let issue =
            parse_issue_ref("https://github.com/org/project/issues/100?tab=comments").unwrap();
        assert_eq!(issue.number, 100);
    }

    #[test]
    fn parse_short_format() {
        let issue = parse_issue_ref("owner/repo#123").unwrap();
        assert_eq!(issue.owner, "owner");
        assert_eq!(issue.repo, "repo");
        assert_eq!(issue.number, 123);
    }

    #[test]
    fn parse_invalid_returns_none() {
        assert!(parse_issue_ref("not-a-url").is_none());
        assert!(parse_issue_ref("#abc").is_none());
        assert!(parse_issue_ref("").is_none());
    }

    #[test]
    fn issue_to_goal_formatting() {
        let details = IssueDetails {
            reference: IssueRef {
                owner: "org".to_string(),
                repo: "project".to_string(),
                number: 42,
            },
            title: "Login page crashes on submit".to_string(),
            body: "When I click submit on the login form, the page crashes.".to_string(),
            labels: vec!["bug".to_string(), "priority:high".to_string()],
            state: "open".to_string(),
            comments_count: 3,
        };

        let goal = issue_to_goal(&details);
        assert!(goal.contains("org/project#42"));
        assert!(goal.contains("Login page crashes"));
        assert!(goal.contains("When I click submit"));
        assert!(goal.contains("bug, priority:high"));
        assert!(goal.contains("3 comments"));
    }

    #[test]
    fn issue_to_goal_truncates_long_body() {
        let details = IssueDetails {
            reference: IssueRef {
                owner: "o".to_string(),
                repo: "r".to_string(),
                number: 1,
            },
            title: "Long issue".to_string(),
            body: "x".repeat(5000),
            labels: vec![],
            state: "open".to_string(),
            comments_count: 0,
        };

        let goal = issue_to_goal(&details);
        assert!(goal.contains("[body truncated]"));
        assert!(goal.len() < 4000);
    }
}
