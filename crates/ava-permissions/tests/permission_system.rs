use ava_permissions::{Action, Pattern, PermissionSystem, Rule};

fn base_rules() -> Vec<Rule> {
    vec![
        Rule {
            tool: Pattern::Glob("read*".into()),
            args: Pattern::Any,
            action: Action::Allow,
        },
        Rule {
            tool: Pattern::Regex("^delete$".into()),
            args: Pattern::Any,
            action: Action::Deny,
        },
        Rule {
            tool: Pattern::Path("execute".into()),
            args: Pattern::Any,
            action: Action::Ask,
        },
    ]
}

#[test]
fn evaluates_allow_deny_ask_actions() {
    let permissions = PermissionSystem::load("/workspace", base_rules());
    assert_eq!(
        permissions.evaluate("read_file", &["./notes.md"]),
        Action::Allow
    );
    assert_eq!(
        permissions.evaluate("delete", &["./notes.md"]),
        Action::Deny
    );
    assert_eq!(permissions.evaluate("execute", &["ls"]), Action::Ask);
}

#[test]
fn respects_rule_order_precedence() {
    let permissions = PermissionSystem::load(
        "/workspace",
        vec![
            Rule {
                tool: Pattern::Any,
                args: Pattern::Any,
                action: Action::Allow,
            },
            Rule {
                tool: Pattern::Any,
                args: Pattern::Any,
                action: Action::Deny,
            },
        ],
    );
    assert_eq!(permissions.evaluate("safe", &["./file.txt"]), Action::Allow);
}

#[test]
fn supports_any_glob_regex_and_path_patterns() {
    let any_permissions = PermissionSystem::load(
        "/workspace",
        vec![Rule {
            tool: Pattern::Any,
            args: Pattern::Any,
            action: Action::Allow,
        }],
    );
    assert_eq!(
        any_permissions.evaluate("list", &["anything"]),
        Action::Allow
    );

    let glob_permissions = PermissionSystem::load(
        "/workspace",
        vec![
            Rule {
                tool: Pattern::Glob("ba*".into()),
                args: Pattern::Glob("*.md".into()),
                action: Action::Deny,
            },
            Rule {
                tool: Pattern::Any,
                args: Pattern::Any,
                action: Action::Allow,
            },
        ],
    );
    assert_eq!(
        glob_permissions.evaluate("bash", &["README.md"]),
        Action::Deny
    );

    let regex_permissions = PermissionSystem::load(
        "/workspace",
        vec![
            Rule {
                tool: Pattern::Regex("^read_[a-z]+$".into()),
                args: Pattern::Regex("^src/.*\\.rs$".into()),
                action: Action::Deny,
            },
            Rule {
                tool: Pattern::Any,
                args: Pattern::Any,
                action: Action::Allow,
            },
        ],
    );
    assert_eq!(
        regex_permissions.evaluate("read_core", &["src/main.rs"]),
        Action::Deny
    );

    let path_permissions = PermissionSystem::load(
        "/workspace",
        vec![
            Rule {
                tool: Pattern::Path("read_file".into()),
                args: Pattern::Path("/workspace/src/lib.rs".into()),
                action: Action::Deny,
            },
            Rule {
                tool: Pattern::Any,
                args: Pattern::Any,
                action: Action::Allow,
            },
        ],
    );
    assert_eq!(
        path_permissions.evaluate("read_file", &["/workspace/src/lib.rs"]),
        Action::Deny
    );
}

#[test]
fn dynamically_escalates_for_workspace_destructive_and_network_actions() {
    let permissions = PermissionSystem::load(
        "/workspace",
        vec![Rule {
            tool: Pattern::Any,
            args: Pattern::Any,
            action: Action::Allow,
        }],
    );
    assert_eq!(
        permissions.evaluate("read_file", &["/tmp/secrets.txt"]),
        Action::Ask
    );
    assert_eq!(
        permissions.evaluate("bash", &["rm -rf ./target"]),
        Action::Ask
    );
    assert_eq!(
        permissions.evaluate("bash", &["curl https://example.com"]),
        Action::Ask
    );
}

#[test]
fn fails_closed_when_dynamic_check_is_invalid_or_errors() {
    let permissions = PermissionSystem::load(
        "/workspace",
        vec![Rule {
            tool: Pattern::Any,
            args: Pattern::Any,
            action: Action::Allow,
        }],
    );
    assert_eq!(permissions.evaluate("bash", &[]), Action::Deny);
    assert_eq!(
        permissions.evaluate("read_file", &["/workspace/\0bad"]),
        Action::Deny
    );
}

#[test]
fn defaults_to_ask_when_no_rule_matches() {
    let permissions = PermissionSystem::load("/workspace", vec![]);
    assert_eq!(permissions.evaluate("unknown", &["arg"]), Action::Ask);
}
