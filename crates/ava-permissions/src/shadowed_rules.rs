//! Detection of shadowed permission rules.
//!
//! A rule is "shadowed" when a broader rule with the same or more permissive
//! action appears earlier in the rule list, making the later rule unreachable.
//! This module identifies such cases so users can clean up their rule sets.

use crate::{Action, Pattern, Rule};

/// A detected shadowed rule, with indices into the original rule list.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ShadowedRule {
    /// Index of the rule that is shadowed (unreachable).
    pub shadowed_index: usize,
    /// Index of the earlier, broader rule that shadows it.
    pub shadowed_by_index: usize,
    /// Human-readable explanation of why the rule is shadowed.
    pub reason: String,
}

/// Detect rules that are shadowed by earlier, broader rules.
///
/// A rule at index `j` is shadowed by a rule at index `i` (where `i < j`) when:
/// 1. The tool pattern of rule `i` covers everything rule `j`'s tool pattern covers.
/// 2. The args pattern of rule `i` covers everything rule `j`'s args pattern covers.
/// 3. Rule `i`'s action is at least as permissive as rule `j`'s action.
///
/// Returns a list of all shadowed rules found.
pub fn detect_shadowed_rules(rules: &[Rule]) -> Vec<ShadowedRule> {
    let mut shadowed = Vec::new();

    for j in 0..rules.len() {
        for i in 0..j {
            if pattern_covers(&rules[i].tool, &rules[j].tool)
                && pattern_covers(&rules[i].args, &rules[j].args)
                && at_least_as_permissive(rules[i].action, rules[j].action)
            {
                let reason = format!(
                    "rule {} ({:?} on {:?}/{:?}) shadows rule {} ({:?} on {:?}/{:?})",
                    i,
                    rules[i].action,
                    rules[i].tool,
                    rules[i].args,
                    j,
                    rules[j].action,
                    rules[j].tool,
                    rules[j].args,
                );
                shadowed.push(ShadowedRule {
                    shadowed_index: j,
                    shadowed_by_index: i,
                    reason,
                });
                // Only report the first (outermost) shadow for each rule
                break;
            }
        }
    }

    shadowed
}

/// Returns true if `broad` covers every candidate that `narrow` would match.
///
/// `Pattern::Any` covers everything. `Glob("*")` covers all specific globs.
/// A glob `a*` covers `ab*` (prefix containment). Identical patterns cover each other.
fn pattern_covers(broad: &Pattern, narrow: &Pattern) -> bool {
    match (broad, narrow) {
        // Any covers everything
        (Pattern::Any, _) => true,
        // Nothing (except Any, handled above) covers Any
        (_, Pattern::Any) => false,

        // Glob("*") covers any specific glob or path
        (Pattern::Glob(b), Pattern::Glob(_)) if b == "*" => true,
        (Pattern::Glob(b), Pattern::Path(_)) if b == "*" => true,

        // Same glob pattern
        (Pattern::Glob(b), Pattern::Glob(n)) => b == n || glob_prefix_covers(b, n),

        // Same path
        (Pattern::Path(b), Pattern::Path(n)) => b == n,

        // Same regex
        (Pattern::Regex(b), Pattern::Regex(n)) => b == n,

        // Cross-type: conservative — only flag obvious cases
        _ => false,
    }
}

/// Check if a broad glob prefix-covers a narrow glob.
///
/// Simple heuristic: if the broad glob ends with `*` and the narrow glob
/// starts with the non-wildcard prefix of the broad glob, then broad covers narrow.
fn glob_prefix_covers(broad: &str, narrow: &str) -> bool {
    if let Some(prefix) = broad.strip_suffix('*') {
        narrow.starts_with(prefix)
    } else {
        false
    }
}

/// Returns true if `a` is at least as permissive as `b`.
///
/// Permissiveness order: Allow > Ask > Deny.
fn at_least_as_permissive(a: Action, b: Action) -> bool {
    permissiveness(a) >= permissiveness(b)
}

fn permissiveness(action: Action) -> u8 {
    match action {
        Action::Allow => 2,
        Action::Ask => 1,
        Action::Deny => 0,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn rule(tool: Pattern, args: Pattern, action: Action) -> Rule {
        Rule { tool, args, action }
    }

    #[test]
    fn any_shadows_glob() {
        let rules = vec![
            rule(Pattern::Any, Pattern::Any, Action::Allow),
            rule(
                Pattern::Glob("bash".to_string()),
                Pattern::Any,
                Action::Allow,
            ),
        ];
        let result = detect_shadowed_rules(&rules);
        assert_eq!(result.len(), 1);
        assert_eq!(result[0].shadowed_index, 1);
        assert_eq!(result[0].shadowed_by_index, 0);
    }

    #[test]
    fn broad_glob_shadows_narrow_glob() {
        let rules = vec![
            rule(Pattern::Glob("*".to_string()), Pattern::Any, Action::Allow),
            rule(
                Pattern::Glob("bash".to_string()),
                Pattern::Any,
                Action::Allow,
            ),
        ];
        let result = detect_shadowed_rules(&rules);
        assert_eq!(result.len(), 1);
        assert_eq!(result[0].shadowed_index, 1);
        assert_eq!(result[0].shadowed_by_index, 0);
    }

    #[test]
    fn glob_prefix_shadows_narrower() {
        let rules = vec![
            rule(
                Pattern::Glob("web_*".to_string()),
                Pattern::Any,
                Action::Ask,
            ),
            rule(
                Pattern::Glob("web_fetch".to_string()),
                Pattern::Any,
                Action::Ask,
            ),
        ];
        let result = detect_shadowed_rules(&rules);
        assert_eq!(result.len(), 1);
        assert_eq!(result[0].shadowed_index, 1);
    }

    #[test]
    fn non_overlapping_rules_not_flagged() {
        let rules = vec![
            rule(
                Pattern::Glob("read".to_string()),
                Pattern::Any,
                Action::Allow,
            ),
            rule(
                Pattern::Glob("write".to_string()),
                Pattern::Any,
                Action::Ask,
            ),
        ];
        let result = detect_shadowed_rules(&rules);
        assert!(result.is_empty());
    }

    #[test]
    fn more_restrictive_earlier_does_not_shadow() {
        let rules = vec![
            rule(Pattern::Any, Pattern::Any, Action::Deny),
            rule(Pattern::Any, Pattern::Any, Action::Allow),
        ];
        let result = detect_shadowed_rules(&rules);
        // Deny is less permissive than Allow, so rule 0 does NOT shadow rule 1
        assert!(result.is_empty());
    }

    #[test]
    fn same_rule_duplicated_is_shadowed() {
        let rules = vec![
            rule(Pattern::Glob("bash".to_string()), Pattern::Any, Action::Ask),
            rule(Pattern::Glob("bash".to_string()), Pattern::Any, Action::Ask),
        ];
        let result = detect_shadowed_rules(&rules);
        assert_eq!(result.len(), 1);
        assert_eq!(result[0].shadowed_index, 1);
    }

    #[test]
    fn args_pattern_must_also_cover() {
        let rules = vec![
            rule(
                Pattern::Any,
                Pattern::Glob("/tmp/*".to_string()),
                Action::Allow,
            ),
            rule(
                Pattern::Any,
                Pattern::Glob("/home/*".to_string()),
                Action::Allow,
            ),
        ];
        let result = detect_shadowed_rules(&rules);
        // Different args patterns — not shadowed
        assert!(result.is_empty());
    }

    #[test]
    fn any_args_shadows_specific_args() {
        let rules = vec![
            rule(Pattern::Any, Pattern::Any, Action::Allow),
            rule(
                Pattern::Any,
                Pattern::Glob("/tmp/*".to_string()),
                Action::Allow,
            ),
        ];
        let result = detect_shadowed_rules(&rules);
        assert_eq!(result.len(), 1);
        assert_eq!(result[0].shadowed_index, 1);
    }

    #[test]
    fn empty_rules_returns_empty() {
        assert!(detect_shadowed_rules(&[]).is_empty());
    }

    #[test]
    fn single_rule_returns_empty() {
        let rules = vec![rule(Pattern::Any, Pattern::Any, Action::Allow)];
        assert!(detect_shadowed_rules(&rules).is_empty());
    }

    #[test]
    fn path_patterns_shadow_when_identical() {
        let rules = vec![
            rule(
                Pattern::Path("/usr/bin/bash".to_string()),
                Pattern::Any,
                Action::Allow,
            ),
            rule(
                Pattern::Path("/usr/bin/bash".to_string()),
                Pattern::Any,
                Action::Ask,
            ),
        ];
        let result = detect_shadowed_rules(&rules);
        assert_eq!(result.len(), 1);
    }
}
