//! AVA Permissions — permission system with rule evaluation.
//!
//! This crate implements:
//! - Static and dynamic permission rules
//! - Bash command risk classification
//! - Workspace boundary enforcement

use regex::Regex;
use std::path::{Component, Path, PathBuf};

pub mod audit;
pub mod audit_store;
pub mod classifier;
pub mod dangerous_paths;
pub mod denial_tracking;
pub mod glob_rules;
pub mod injection;
pub mod inspector;
pub mod path_safety;
pub mod persistent;
pub mod policy;
pub mod sanitization;
pub mod secret_scanner;
pub mod shadowed_rules;
pub mod ssrf;
pub mod tags;

use classifier::classify_bash_command;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Action {
    Allow,
    Deny,
    Ask,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Pattern {
    Any,
    Glob(String),
    Regex(String),
    Path(String),
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Rule {
    pub tool: Pattern,
    pub args: Pattern,
    pub action: Action,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct PermissionSystem {
    workspace_root: PathBuf,
    rules: Vec<Rule>,
}

impl PermissionSystem {
    pub fn load(workspace_root: impl Into<PathBuf>, rules: Vec<Rule>) -> Self {
        Self {
            workspace_root: workspace_root.into(),
            rules,
        }
    }

    pub fn evaluate(&self, tool: &str, args: &[&str]) -> Action {
        let Ok(static_action) = self.static_action(tool, args) else {
            return Action::Deny;
        };

        match self.dynamic_check(tool, args) {
            Ok(Some(dynamic_action)) => most_restrictive(static_action, dynamic_action),
            Ok(None) => static_action,
            Err(_) => Action::Deny,
        }
    }

    pub fn dynamic_check(&self, tool: &str, args: &[&str]) -> Result<Option<Action>, &'static str> {
        if args.iter().any(|arg| arg.contains('\0')) {
            return Err("invalid argument");
        }

        if self.has_out_of_workspace_path(args) {
            return Ok(Some(Action::Ask));
        }

        if tool == "bash" {
            let command = args.first().ok_or("missing bash command")?;

            let classification = classify_bash_command(command);
            if classification.blocked {
                return Ok(Some(Action::Deny));
            }
            if classification.risk_level >= crate::tags::RiskLevel::High {
                return Ok(Some(Action::Ask));
            }
        }

        if ["web_fetch", "web_search", "curl", "wget"].contains(&tool) {
            return Ok(Some(Action::Ask));
        }

        Ok(None)
    }

    fn static_action(&self, tool: &str, args: &[&str]) -> Result<Action, &'static str> {
        for rule in &self.rules {
            let tool_matches = pattern_matches(&rule.tool, tool, &self.workspace_root)?;
            let args_matches = args_match(&rule.args, args, &self.workspace_root)?;
            if tool_matches && args_matches {
                return Ok(rule.action);
            }
        }
        Ok(Action::Ask)
    }

    fn has_out_of_workspace_path(&self, args: &[&str]) -> bool {
        let workspace_root = normalize_path(&self.workspace_root);
        args.iter()
            .filter(|arg| looks_like_path(arg))
            .map(|arg| normalize_with_base(arg, &workspace_root))
            .any(|candidate| !candidate.starts_with(&workspace_root))
    }
}

fn args_match(
    pattern: &Pattern,
    args: &[&str],
    workspace_root: &Path,
) -> Result<bool, &'static str> {
    match pattern {
        Pattern::Any => Ok(true),
        _ => {
            for arg in args {
                if pattern_matches(pattern, arg, workspace_root)? {
                    return Ok(true);
                }
            }
            Ok(false)
        }
    }
}

fn pattern_matches(
    pattern: &Pattern,
    candidate: &str,
    workspace_root: &Path,
) -> Result<bool, &'static str> {
    match pattern {
        Pattern::Any => Ok(true),
        Pattern::Glob(glob) => Ok(glob_matches(glob, candidate)),
        Pattern::Regex(expr) => Regex::new(expr)
            .map(|regex| regex.is_match(candidate))
            .map_err(|_| "invalid regex"),
        Pattern::Path(path) => {
            let base = normalize_path(workspace_root);
            Ok(normalize_with_base(candidate, &base) == normalize_with_base(path, &base))
        }
    }
}

fn normalize_with_base(value: &str, base: &Path) -> PathBuf {
    let path = Path::new(value);
    if path.is_absolute() {
        normalize_path(path)
    } else {
        normalize_path(&base.join(path))
    }
}

fn normalize_path(path: &Path) -> PathBuf {
    let mut out = PathBuf::new();
    for part in path.components() {
        match part {
            Component::CurDir => {}
            Component::ParentDir => {
                out.pop();
            }
            Component::Normal(part) => out.push(part),
            Component::RootDir => out.push(Path::new("/")),
            Component::Prefix(prefix) => out.push(prefix.as_os_str()),
        }
    }
    out
}

fn glob_matches(pattern: &str, value: &str) -> bool {
    let pattern = pattern.as_bytes();
    let value = value.as_bytes();
    let mut p = 0usize;
    let mut v = 0usize;
    let mut star: Option<usize> = None;
    let mut backtrack = 0usize;

    while v < value.len() {
        if p < pattern.len() && (pattern[p] == b'?' || pattern[p] == value[v]) {
            p += 1;
            v += 1;
            continue;
        }

        if p < pattern.len() && pattern[p] == b'*' {
            star = Some(p);
            p += 1;
            backtrack = v;
            continue;
        }

        if let Some(star_pos) = star {
            p = star_pos + 1;
            backtrack += 1;
            v = backtrack;
            continue;
        }

        return false;
    }

    while p < pattern.len() && pattern[p] == b'*' {
        p += 1;
    }

    p == pattern.len()
}

fn most_restrictive(static_action: Action, dynamic_action: Action) -> Action {
    match (static_action, dynamic_action) {
        (Action::Deny, _) | (_, Action::Deny) => Action::Deny,
        (Action::Ask, _) | (_, Action::Ask) => Action::Ask,
        (Action::Allow, Action::Allow) => Action::Allow,
    }
}

fn looks_like_path(value: &str) -> bool {
    value.starts_with('/')
        || value.starts_with("./")
        || value.starts_with("../")
        || value.contains('/')
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn glob_matches_exact_literal() {
        assert!(glob_matches("bash", "bash"));
        assert!(!glob_matches("bash", "grep"));
    }

    #[test]
    fn glob_matches_star_and_question_mark() {
        assert!(glob_matches("b*", "bash"));
        assert!(glob_matches("ba?h", "bash"));
        assert!(!glob_matches("ba?h", "baash"));
    }

    #[test]
    fn most_restrictive_prefers_safer_action() {
        assert_eq!(
            most_restrictive(Action::Allow, Action::Allow),
            Action::Allow
        );
        assert_eq!(most_restrictive(Action::Ask, Action::Ask), Action::Ask);
        assert_eq!(most_restrictive(Action::Allow, Action::Ask), Action::Ask);
        assert_eq!(most_restrictive(Action::Allow, Action::Deny), Action::Deny);
        assert_eq!(most_restrictive(Action::Ask, Action::Allow), Action::Ask);
        assert_eq!(most_restrictive(Action::Deny, Action::Ask), Action::Deny);
        assert_eq!(most_restrictive(Action::Deny, Action::Deny), Action::Deny);
        assert_eq!(most_restrictive(Action::Ask, Action::Deny), Action::Deny);
        assert_eq!(most_restrictive(Action::Deny, Action::Allow), Action::Deny);
    }

    #[test]
    fn looks_like_path_catches_common_path_forms() {
        assert!(looks_like_path("/tmp/file.txt"));
        assert!(looks_like_path("./src/lib.rs"));
        assert!(looks_like_path("../README.md"));
        assert!(looks_like_path("nested/file.txt"));
        assert!(!looks_like_path("README.md"));
        assert!(!looks_like_path(""));
    }

    #[test]
    fn normalize_with_base_resolves_relative_segments() {
        let base = Path::new("/workspace/project");
        assert_eq!(
            normalize_with_base("./src/../README.md", base),
            PathBuf::from("/workspace/project/README.md")
        );
        assert_eq!(
            normalize_with_base("/tmp/../var/log", base),
            PathBuf::from("/var/log")
        );
    }

    #[test]
    fn dynamic_check_denies_bash_blocked_commands() {
        let system = PermissionSystem::load("/workspace/project", vec![]);
        let result = system.dynamic_check("bash", &["rm -rf /"]).unwrap();
        assert_eq!(result, Some(Action::Deny));
    }

    #[test]
    fn dynamic_check_rejects_null_byte_arguments() {
        let system = PermissionSystem::load("/workspace/project", vec![]);
        assert!(system.dynamic_check("read", &["bad\0path"]).is_err());
        assert_eq!(system.evaluate("read", &["bad\0path"]), Action::Deny);
    }

    #[test]
    fn dynamic_check_asks_for_web_tools_and_workspace_escape() {
        let system = PermissionSystem::load("/workspace/project", vec![]);
        assert_eq!(
            system
                .dynamic_check("web_fetch", &["https://example.com"])
                .unwrap(),
            Some(Action::Ask)
        );
        assert_eq!(
            system.dynamic_check("read", &["../secrets.txt"]).unwrap(),
            Some(Action::Ask)
        );
    }

    #[test]
    fn evaluate_respects_first_matching_static_rule() {
        let system = PermissionSystem::load(
            "/workspace/project",
            vec![
                Rule {
                    tool: Pattern::Glob("read*".to_string()),
                    args: Pattern::Any,
                    action: Action::Allow,
                },
                Rule {
                    tool: Pattern::Glob("read".to_string()),
                    args: Pattern::Any,
                    action: Action::Deny,
                },
            ],
        );

        assert_eq!(system.evaluate("read", &["README.md"]), Action::Allow);
    }

    #[test]
    fn evaluate_combines_static_allow_with_dynamic_ask() {
        let system = PermissionSystem::load(
            "/workspace/project",
            vec![Rule {
                tool: Pattern::Glob("web_fetch".to_string()),
                args: Pattern::Any,
                action: Action::Allow,
            }],
        );

        assert_eq!(
            system.evaluate("web_fetch", &["https://example.com"]),
            Action::Ask
        );
    }

    #[test]
    fn evaluate_path_pattern_matches_normalized_relative_paths() {
        let system = PermissionSystem::load(
            "/workspace/project",
            vec![Rule {
                tool: Pattern::Glob("read".to_string()),
                args: Pattern::Path("src/lib.rs".to_string()),
                action: Action::Allow,
            }],
        );

        assert_eq!(
            system.evaluate("read", &["./src/../src/lib.rs"]),
            Action::Allow
        );
        assert_eq!(system.evaluate("read", &["./src/main.rs"]), Action::Ask);
    }

    #[test]
    fn evaluate_regex_rule_matches_argument() {
        let system = PermissionSystem::load(
            "/workspace/project",
            vec![Rule {
                tool: Pattern::Glob("bash".to_string()),
                args: Pattern::Regex("git status|git diff".to_string()),
                action: Action::Allow,
            }],
        );

        assert_eq!(system.evaluate("bash", &["git status"]), Action::Allow);
        // Regex matching is substring-based because pattern_matches uses Regex::is_match.
        assert_eq!(
            system.evaluate("bash", &["git status --short"]),
            Action::Allow
        );
        assert_eq!(
            system.evaluate("bash", &["please run git diff now"]),
            Action::Allow
        );
        assert_eq!(system.evaluate("bash", &["rm -rf /"]), Action::Deny);
    }

    #[test]
    fn evaluate_invalid_regex_fails_closed_to_deny() {
        let system = PermissionSystem::load(
            "/workspace/project",
            vec![Rule {
                tool: Pattern::Regex("[invalid".to_string()),
                args: Pattern::Any,
                action: Action::Allow,
            }],
        );

        assert_eq!(system.evaluate("read", &["README.md"]), Action::Deny);
    }
}
