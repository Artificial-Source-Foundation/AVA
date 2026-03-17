//! AVA Permissions — permission system with rule evaluation.
//!
//! This crate implements:
//! - Static and dynamic permission rules
//! - Bash command risk classification
//! - Workspace boundary enforcement

use regex::Regex;
use std::path::{Component, Path, PathBuf};

pub mod arc_monitor;
pub mod audit;
pub mod canonicalize;
pub mod checker_registry;
pub mod classifier;
pub mod guardian;
pub mod injection;
pub mod inspector;
pub mod osv_scanner;
pub mod path_safety;
pub mod patterns;
pub mod persistent;
pub mod policy;
pub mod readonly_judge;
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
