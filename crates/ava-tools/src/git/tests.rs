use std::path::Path;
use std::process::Command;

use tempfile::tempdir;

use super::{GhostSnapshotter, GitAction, GitTool, GitToolError, GHOST_SNAPSHOT_PREFIX};

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

#[tokio::test]
async fn ghost_snapshotter_creates_hidden_blob_ref() {
    let temp = tempdir().expect("tempdir");
    run_git(temp.path(), &["init"]);

    let file_path = temp.path().join("src/lib.rs");
    std::fs::create_dir_all(file_path.parent().expect("parent")).expect("mkdir");
    std::fs::write(&file_path, "fn before() {}\n").expect("write file");

    let snapshot = GhostSnapshotter::new()
        .snapshot_file_before_write(&file_path, "fn before() {}\n")
        .await
        .expect("snapshot should succeed")
        .expect("git repo should produce snapshot");

    assert!(snapshot.ref_name.starts_with(GHOST_SNAPSHOT_PREFIX));

    let blob = Command::new("git")
        .arg("-C")
        .arg(temp.path())
        .args(["cat-file", "-p", snapshot.ref_name.as_str()])
        .output()
        .expect("cat-file output");
    assert!(blob.status.success(), "cat-file failed: {:?}", blob);
    assert_eq!(String::from_utf8_lossy(&blob.stdout), "fn before() {}\n");
}

#[tokio::test]
async fn ghost_snapshotter_skips_non_git_paths() {
    let temp = tempdir().expect("tempdir");
    let file_path = temp.path().join("plain.txt");
    std::fs::write(&file_path, "hello\n").expect("write file");

    let snapshot = GhostSnapshotter::new()
        .snapshot_file_before_write(&file_path, "hello\n")
        .await
        .expect("non-git lookup should not fail");

    assert!(snapshot.is_none());
}

#[test]
fn resolves_bare_relative_paths_against_current_dir() {
    let resolved = super::snapshot::resolve_input_path(Path::new("relative.txt"))
        .expect("relative paths should resolve");

    assert!(resolved.is_absolute());
    assert!(resolved.ends_with("relative.txt"));
}

#[tokio::test]
async fn ghost_snapshotter_uses_unique_refs_for_rapid_snapshots() {
    let temp = tempdir().expect("tempdir");
    run_git(temp.path(), &["init"]);

    let file_path = temp.path().join("src/lib.rs");
    std::fs::create_dir_all(file_path.parent().expect("parent")).expect("mkdir");
    std::fs::write(&file_path, "fn before() {}\n").expect("write file");

    let snapshotter = GhostSnapshotter::new();
    let first = snapshotter
        .snapshot_file_before_write(&file_path, "fn before() {}\n")
        .await
        .expect("first snapshot should succeed")
        .expect("git repo should produce snapshot");
    let second = snapshotter
        .snapshot_file_before_write(&file_path, "fn before() {}\n")
        .await
        .expect("second snapshot should succeed")
        .expect("git repo should produce snapshot");

    assert_ne!(first.ref_name, second.ref_name);
}

fn run_git(repo: &Path, args: &[&str]) {
    let output = Command::new("git")
        .arg("-C")
        .arg(repo)
        .args(args)
        .output()
        .expect("git command should run");
    assert!(
        output.status.success(),
        "git {:?} failed: {}",
        args,
        String::from_utf8_lossy(&output.stderr)
    );
}
