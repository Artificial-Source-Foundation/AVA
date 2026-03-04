use super::{GitAction, GitTool, GitToolError};

#[test]
fn parses_and_routes_supported_actions() {
    let cases = [
        (
            r#"{"action":"commit","args":["-m","msg"]}"#,
            "git",
            "commit",
        ),
        (r#"{"action":"branch"}"#, "git", "branch"),
        (
            r#"{"action":"checkout","args":["main"]}"#,
            "git",
            "checkout",
        ),
        (r#"{"action":"status","args":["--short"]}"#, "git", "status"),
        (r#"{"action":"diff"}"#, "git", "diff"),
        (r#"{"action":"log","args":["-n","1"]}"#, "git", "log"),
        (r#"{"action":"pr","args":["list"]}"#, "gh", "pr"),
    ];

    for (payload, expected_program, expected_subcommand) in cases {
        let action = GitAction::from_json(payload).expect("action should parse");
        let (program, args) = GitTool::dispatch(&action);
        assert_eq!(program, expected_program);
        assert_eq!(args.first().map(String::as_str), Some(expected_subcommand));
    }
}

#[test]
fn returns_unsupported_action_error() {
    let error = GitAction::from_json(r#"{"action":"rebase"}"#).expect_err("must error");
    assert!(matches!(error, GitToolError::UnsupportedAction(action) if action == "rebase"));
}

#[test]
fn returns_error_on_malformed_json() {
    let error = GitAction::from_json("not-json").expect_err("must error");
    assert!(matches!(error, GitToolError::InvalidActionPayload(_)));
}

#[tokio::test]
async fn returns_error_on_non_zero_subprocess_status() {
    let tool = GitTool::new();
    let error = tool
        .run(GitAction::Status(vec![
            "--definitely-invalid-flag".to_string()
        ]))
        .await
        .expect_err("must fail");

    assert!(matches!(error, GitToolError::CommandFailed { program, .. } if program == "git"));
}
