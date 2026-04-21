//! Wildcard/glob permission rules for file path access control.
//!
//! Users define rules in `$XDG_CONFIG_HOME/ava/permissions.toml` (global) and
//! `.ava/permissions.toml` (project-local). Each rule maps a glob pattern
//! to an action (allow, ask, deny). First matching rule wins.
//!
//! Supported patterns:
//! - `*` matches any characters except `/`
//! - `**` matches any characters including `/` (recursive)
//! - `?` matches a single character (not `/`)
//!
//! # Example
//!
//! ```toml
//! [[path_rules]]
//! pattern = "*.env"
//! action = "deny"
//!
//! [[path_rules]]
//! pattern = "src/**/*.rs"
//! action = "allow"
//! ```

use std::path::Path;

use serde::{Deserialize, Serialize};

/// Action to take when a glob rule matches a file path.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum GlobAction {
    Allow,
    Ask,
    Deny,
}

/// A single glob permission rule: pattern + action.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct GlobRule {
    /// Glob pattern (e.g., `*.env`, `src/**/*.rs`, `/etc/*`).
    pub pattern: String,
    /// Action to take when the pattern matches.
    pub action: GlobAction,
}

/// TOML structure for the `[[path_rules]]` section of `permissions.toml`.
#[derive(Debug, Default, Clone, Serialize, Deserialize)]
pub struct GlobRulesConfig {
    /// Ordered list of glob rules. First match wins.
    #[serde(default)]
    pub path_rules: Vec<GlobRule>,
}

/// A set of glob permission rules evaluated in order (first match wins).
#[derive(Debug, Clone)]
pub struct GlobRuleset {
    rules: Vec<GlobRule>,
}

impl GlobRuleset {
    /// Create a new ruleset from an ordered list of rules.
    pub fn new(rules: Vec<GlobRule>) -> Self {
        Self { rules }
    }

    /// Create an empty ruleset (no rules, always passes through).
    pub fn empty() -> Self {
        Self { rules: vec![] }
    }

    /// Check a file path against the ruleset. Returns the action of the first
    /// matching rule, or `None` if no rule matches (fall through to existing logic).
    pub fn check(&self, path: &str) -> Option<GlobAction> {
        // Normalize the path for matching — strip leading `./`
        let normalized = path.strip_prefix("./").unwrap_or(path);

        for rule in &self.rules {
            if glob_match(&rule.pattern, normalized) {
                return Some(rule.action);
            }
        }
        None
    }

    /// Returns true if this ruleset has no rules.
    pub fn is_empty(&self) -> bool {
        self.rules.is_empty()
    }

    /// Returns a reference to the ordered list of rules.
    pub fn rules(&self) -> &[GlobRule] {
        &self.rules
    }

    /// Append additional rules to the end of the ruleset.
    /// Since first match wins, existing rules take priority over appended ones.
    pub fn extend(&mut self, rules: impl IntoIterator<Item = GlobRule>) {
        self.rules.extend(rules);
    }

    /// Load glob rules from a TOML file path. Returns empty ruleset on error.
    pub fn load_from(path: &Path) -> Self {
        let Ok(content) = std::fs::read_to_string(path) else {
            return Self::empty();
        };
        Self::parse_toml(&content)
    }

    /// Load user-global rules from `$XDG_CONFIG_HOME/ava/permissions.toml`.
    pub fn load_global() -> Self {
        let Some(preferred) = dirs::config_dir().map(|dir| dir.join("ava/permissions.toml")) else {
            return Self::empty();
        };
        let path = if preferred.exists() {
            preferred
        } else if let Some(legacy) = dirs::home_dir()
            .map(|home| home.join(".ava/permissions.toml"))
            .filter(|path| path.exists())
        {
            legacy
        } else {
            preferred
        };
        Self::load_from(&path)
    }

    /// Load project-local rules from `.ava/permissions.toml` under workspace root.
    ///
    /// SEC: Project-local path rules can only *restrict* (deny/ask), never allow.
    /// Any `allow` actions are upgraded to `ask` to prevent a malicious repo from
    /// pre-approving access to sensitive files.
    pub fn load_project(workspace_root: &Path) -> Self {
        let path = workspace_root.join(".ava/permissions.toml");
        let Ok(content) = std::fs::read_to_string(&path) else {
            return Self::empty();
        };
        let mut ruleset = Self::parse_toml(&content);
        // Project-local rules cannot grant allow — upgrade to ask
        for rule in &mut ruleset.rules {
            if rule.action == GlobAction::Allow {
                rule.action = GlobAction::Ask;
            }
        }
        ruleset
    }

    /// Load merged rules: global rules first, then project-local restrictions.
    /// Global rules take priority (checked first). Project-local rules can only
    /// add deny/ask restrictions.
    pub fn load_merged(workspace_root: &Path) -> Self {
        let global = Self::load_global();
        let project = Self::load_project(workspace_root);
        let mut rules = global.rules;
        rules.extend(project.rules);
        Self::new(rules)
    }

    /// Parse glob rules from TOML content.
    fn parse_toml(content: &str) -> Self {
        match toml::from_str::<GlobRulesConfig>(content) {
            Ok(config) => Self::new(config.path_rules),
            Err(_) => Self::empty(),
        }
    }
}

/// Match a glob pattern against a value.
///
/// - `*` matches any characters except `/`
/// - `**` matches any characters including `/`
/// - `?` matches a single character (not `/`)
///
/// The pattern is matched against the full value (anchored at both ends).
fn glob_match(pattern: &str, value: &str) -> bool {
    // Split pattern into segments on `**`. Each segment between `**` markers
    // is matched with simple glob (where `*` doesn't cross `/`).
    // `**` consumes zero or more path segments.
    let segments = split_on_doublestar(pattern);

    if segments.len() == 1 {
        // No ** in pattern — simple match
        return matches_simple(segments[0], value);
    }

    // Multiple segments separated by **
    // First segment must match the start of value
    // Last segment must match the end of value
    // Middle segments must match somewhere in between
    let first = segments[0];
    let last = segments[segments.len() - 1];
    let middle = &segments[1..segments.len() - 1];

    // Try all possible positions
    glob_match_segments(first, middle, last, value)
}

/// Split a pattern on `**/` or `/**` or standalone `**` boundaries.
/// Returns the segments between `**` markers.
fn split_on_doublestar(pattern: &str) -> Vec<&str> {
    let mut segments = Vec::new();
    let mut rest = pattern;

    loop {
        if let Some(pos) = find_doublestar_str(rest) {
            segments.push(&rest[..pos]);
            let mut after = pos + 2;
            // Skip trailing / after **
            if after < rest.len() && rest.as_bytes()[after] == b'/' {
                after += 1;
            }
            rest = &rest[after..];
        } else {
            segments.push(rest);
            break;
        }
    }

    segments
}

/// Find `**` in a string pattern that is at a path boundary.
fn find_doublestar_str(s: &str) -> Option<usize> {
    let bytes = s.as_bytes();
    let mut i = 0;
    while i + 1 < bytes.len() {
        if bytes[i] == b'*' && bytes[i + 1] == b'*' {
            let before_ok = i == 0 || bytes[i - 1] == b'/';
            let after_ok = i + 2 >= bytes.len() || bytes[i + 2] == b'/';
            if before_ok && after_ok {
                return Some(i);
            }
        }
        i += 1;
    }
    None
}

/// Match segments separated by `**` against a value.
fn glob_match_segments(first: &str, middle: &[&str], last: &str, value: &str) -> bool {
    // First segment: must match a prefix of value (up to a / boundary)
    // Handle leading `/` in first for absolute paths
    if first.is_empty() && last.is_empty() && middle.is_empty() {
        // Pattern is just "**" — matches everything
        return true;
    }

    // Try all positions where first matches a prefix
    let first_positions = if first.is_empty() {
        // ** at the start: first segment is empty, can start from position 0
        vec![0usize]
    } else {
        // Find all positions where first matches value[..pos]
        let mut positions = Vec::new();
        for end in 0..=value.len() {
            if matches_simple(first, &value[..end]) {
                // Must end at a / boundary or at the end of value
                if end == value.len()
                    || (end < value.len() && value.as_bytes()[end] == b'/')
                    || first.ends_with('/')
                {
                    // Skip the / separator after the match
                    let skip = if end < value.len() && value.as_bytes()[end] == b'/' {
                        end + 1
                    } else {
                        end
                    };
                    positions.push(skip);
                }
            }
        }
        positions
    };

    for start in first_positions {
        if match_remaining_segments(middle, last, &value[start..]) {
            return true;
        }
    }

    false
}

/// Match middle segments and final segment against the remaining value.
fn match_remaining_segments(middle: &[&str], last: &str, value: &str) -> bool {
    if middle.is_empty() {
        // Just need to match `last` against the end of value (with ** before it)
        if last.is_empty() {
            return true; // ** at the end matches everything
        }
        // Try matching last against every possible suffix
        for start in 0..=value.len() {
            if matches_simple(last, &value[start..]) {
                return true;
            }
        }
        return false;
    }

    // Find where the first middle segment matches
    let seg = middle[0];
    let rest_middle = &middle[1..];

    for start in 0..=value.len() {
        for end in start..=value.len() {
            if matches_simple(seg, &value[start..end]) {
                let skip = if end < value.len() && value.as_bytes()[end] == b'/' {
                    end + 1
                } else {
                    end
                };
                if match_remaining_segments(rest_middle, last, &value[skip..]) {
                    return true;
                }
            }
        }
    }

    false
}

/// Simple glob matching where `*` matches anything except `/`
/// and `?` matches any single character except `/`.
fn matches_simple(pattern: &str, value: &str) -> bool {
    matches_simple_bytes(pattern.as_bytes(), value.as_bytes())
}

fn matches_simple_bytes(pattern: &[u8], value: &[u8]) -> bool {
    let mut p = 0usize;
    let mut v = 0usize;
    let mut star: Option<(usize, usize)> = None; // (pattern_pos, value_pos)

    while v < value.len() {
        if p < pattern.len() && pattern[p] == b'?' && value[v] != b'/' {
            p += 1;
            v += 1;
            continue;
        }

        if p < pattern.len() && pattern[p] != b'*' && pattern[p] != b'?' && pattern[p] == value[v] {
            p += 1;
            v += 1;
            continue;
        }

        if p < pattern.len() && pattern[p] == b'*' {
            star = Some((p, v));
            p += 1;
            continue;
        }

        if let Some((sp, sv)) = star {
            // Backtrack: * cannot match /
            let next_v = sv + 1;
            if next_v <= value.len() && value[sv] != b'/' {
                p = sp + 1;
                v = next_v;
                star = Some((sp, next_v));
                continue;
            }
        }

        return false;
    }

    // Consume trailing *s in pattern
    while p < pattern.len() && pattern[p] == b'*' {
        p += 1;
    }

    p == pattern.len()
}

#[cfg(test)]
mod tests {
    use super::*;

    // === Glob matching ===

    #[test]
    fn star_matches_filename() {
        assert!(glob_match("*.env", ".env"));
        assert!(glob_match("*.env", "test.env"));
        assert!(glob_match("*.rs", "main.rs"));
        assert!(!glob_match("*.env", "src/.env"));
        assert!(!glob_match("*.rs", "src/main.rs"));
    }

    #[test]
    fn star_does_not_cross_slash() {
        assert!(!glob_match("*.rs", "src/main.rs"));
        assert!(glob_match("src/*.rs", "src/main.rs"));
        assert!(!glob_match("src/*.rs", "src/sub/main.rs"));
    }

    #[test]
    fn doublestar_matches_recursively() {
        assert!(glob_match("src/**/*.rs", "src/main.rs"));
        assert!(glob_match("src/**/*.rs", "src/sub/main.rs"));
        assert!(glob_match("src/**/*.rs", "src/a/b/c/main.rs"));
        assert!(!glob_match("src/**/*.rs", "lib/main.rs"));
    }

    #[test]
    fn doublestar_at_start() {
        assert!(glob_match("**/*.env", ".env"));
        assert!(glob_match("**/*.env", "src/.env"));
        assert!(glob_match("**/*.env", "a/b/c/.env"));
    }

    #[test]
    fn doublestar_at_end() {
        assert!(glob_match("src/**", "src/main.rs"));
        assert!(glob_match("src/**", "src/sub/main.rs"));
        assert!(glob_match("src/**", "src/a/b/c"));
    }

    #[test]
    fn question_mark_matches_single_char() {
        assert!(glob_match("?.rs", "a.rs"));
        assert!(!glob_match("?.rs", "ab.rs"));
        assert!(!glob_match("?.rs", ".rs"));
        // ? does not match /
        assert!(!glob_match("src?main.rs", "src/main.rs"));
    }

    #[test]
    fn exact_match() {
        assert!(glob_match("Cargo.toml", "Cargo.toml"));
        assert!(!glob_match("Cargo.toml", "cargo.toml"));
        assert!(!glob_match("Cargo.toml", "src/Cargo.toml"));
    }

    #[test]
    fn absolute_path_patterns() {
        assert!(glob_match("/etc/*", "/etc/passwd"));
        assert!(glob_match("/etc/*", "/etc/shadow"));
        assert!(!glob_match("/etc/*", "/etc/nginx/nginx.conf"));
        assert!(glob_match("/etc/**", "/etc/nginx/nginx.conf"));
    }

    #[test]
    fn complex_patterns() {
        assert!(glob_match("**/*.secret", "config/db.secret"));
        assert!(glob_match("**/*.secret", ".secret"));
        assert!(glob_match("test_*.py", "test_main.py"));
        assert!(!glob_match("test_*.py", "src/test_main.py"));
    }

    // === GlobRuleset ===

    #[test]
    fn empty_ruleset_returns_none() {
        let ruleset = GlobRuleset::empty();
        assert_eq!(ruleset.check("anything.rs"), None);
        assert!(ruleset.is_empty());
    }

    #[test]
    fn first_match_wins() {
        let ruleset = GlobRuleset::new(vec![
            GlobRule {
                pattern: "*.env".to_string(),
                action: GlobAction::Deny,
            },
            GlobRule {
                pattern: "*.env".to_string(),
                action: GlobAction::Allow,
            },
        ]);
        assert_eq!(ruleset.check("test.env"), Some(GlobAction::Deny));
    }

    #[test]
    fn multiple_rules_different_patterns() {
        let ruleset = GlobRuleset::new(vec![
            GlobRule {
                pattern: "*.env".to_string(),
                action: GlobAction::Deny,
            },
            GlobRule {
                pattern: "src/**/*.rs".to_string(),
                action: GlobAction::Allow,
            },
            GlobRule {
                pattern: "/etc/*".to_string(),
                action: GlobAction::Deny,
            },
        ]);

        assert_eq!(ruleset.check(".env"), Some(GlobAction::Deny));
        assert_eq!(ruleset.check("production.env"), Some(GlobAction::Deny));
        assert_eq!(ruleset.check("src/main.rs"), Some(GlobAction::Allow));
        assert_eq!(ruleset.check("src/sub/lib.rs"), Some(GlobAction::Allow));
        assert_eq!(ruleset.check("/etc/passwd"), Some(GlobAction::Deny));
        assert_eq!(ruleset.check("README.md"), None);
    }

    #[test]
    fn strips_dot_slash_prefix() {
        let ruleset = GlobRuleset::new(vec![GlobRule {
            pattern: "*.env".to_string(),
            action: GlobAction::Deny,
        }]);
        assert_eq!(ruleset.check("./.env"), Some(GlobAction::Deny));
        assert_eq!(ruleset.check("./test.env"), Some(GlobAction::Deny));
    }

    // === TOML parsing ===

    #[test]
    fn parse_toml_config() {
        let toml = r#"
[[path_rules]]
pattern = "*.env"
action = "deny"

[[path_rules]]
pattern = "*.secret"
action = "deny"

[[path_rules]]
pattern = "src/**/*.rs"
action = "allow"

[[path_rules]]
pattern = "/etc/*"
action = "deny"
"#;
        let ruleset = GlobRuleset::parse_toml(toml);
        assert_eq!(ruleset.rules.len(), 4);
        assert_eq!(ruleset.check("test.env"), Some(GlobAction::Deny));
        assert_eq!(ruleset.check("src/main.rs"), Some(GlobAction::Allow));
    }

    #[test]
    fn parse_invalid_toml_returns_empty() {
        let ruleset = GlobRuleset::parse_toml("not valid toml {{{");
        assert!(ruleset.is_empty());
    }

    #[test]
    fn parse_toml_without_path_rules_returns_empty() {
        let toml = r#"
[some_other_section]
key = "value"
"#;
        let ruleset = GlobRuleset::parse_toml(toml);
        assert!(ruleset.is_empty());
    }

    // === File loading ===

    #[test]
    fn load_from_missing_file_returns_empty() {
        let ruleset = GlobRuleset::load_from(Path::new("/nonexistent/permissions.toml"));
        assert!(ruleset.is_empty());
    }

    #[test]
    fn load_and_check_from_file() {
        let dir = std::env::temp_dir().join("ava_glob_rules_test");
        let _ = std::fs::remove_dir_all(&dir);
        std::fs::create_dir_all(&dir).unwrap();

        let path = dir.join("permissions.toml");
        std::fs::write(
            &path,
            r#"
[[path_rules]]
pattern = "*.env"
action = "deny"

[[path_rules]]
pattern = "src/**/*.rs"
action = "allow"
"#,
        )
        .unwrap();

        let ruleset = GlobRuleset::load_from(&path);
        assert_eq!(ruleset.rules.len(), 2);
        assert_eq!(ruleset.check("test.env"), Some(GlobAction::Deny));
        assert_eq!(ruleset.check("src/lib.rs"), Some(GlobAction::Allow));
        assert_eq!(ruleset.check("README.md"), None);

        let _ = std::fs::remove_dir_all(&dir);
    }

    // === Security: project-local rules ===

    #[test]
    fn project_local_upgrades_allow_to_ask() {
        let dir = std::env::temp_dir().join("ava_glob_project_sec_test");
        let _ = std::fs::remove_dir_all(&dir);
        let ava_dir = dir.join(".ava");
        std::fs::create_dir_all(&ava_dir).unwrap();

        std::fs::write(
            ava_dir.join("permissions.toml"),
            r#"
[[path_rules]]
pattern = "src/**/*.rs"
action = "allow"

[[path_rules]]
pattern = "*.env"
action = "deny"
"#,
        )
        .unwrap();

        let ruleset = GlobRuleset::load_project(&dir);
        // Allow upgraded to Ask for project-local rules
        assert_eq!(ruleset.check("src/main.rs"), Some(GlobAction::Ask));
        // Deny stays as deny
        assert_eq!(ruleset.check("test.env"), Some(GlobAction::Deny));

        let _ = std::fs::remove_dir_all(&dir);
    }

    // === Coexistence with existing PersistentRules ===

    #[test]
    fn toml_with_both_persistent_and_glob_rules() {
        let toml = r#"
allowed_tools = ["bash"]
allowed_commands = ["cargo test"]
blocked_tools = []
blocked_commands = []

[[path_rules]]
pattern = "*.env"
action = "deny"
"#;
        // GlobRuleset ignores fields it doesn't care about
        let ruleset = GlobRuleset::parse_toml(toml);
        assert_eq!(ruleset.rules.len(), 1);
        assert_eq!(ruleset.check("test.env"), Some(GlobAction::Deny));
    }
}
