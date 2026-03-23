use std::path::Path;

use crate::error::Result;
use crate::policy::{validate_policy, validate_request};
use crate::types::{SandboxPlan, SandboxPolicy, SandboxRequest};

/// Directories within writable mounts that should be read-only to prevent
/// the sandboxed process from corrupting git history or AVA config/credentials.
const PROTECTED_SUBDIRS: &[&str] = &[".git", ".ava"];

pub fn build_bwrap_plan(request: &SandboxRequest, policy: &SandboxPolicy) -> Result<SandboxPlan> {
    validate_policy(policy)?;
    validate_request(request)?;

    let mut args = vec![
        "--unshare-user".to_string(),
        "--unshare-pid".to_string(),
        "--die-with-parent".to_string(),
        // Minimal /dev and /proc for sandboxed processes
        "--dev".to_string(),
        "/dev".to_string(),
        "--proc".to_string(),
        "/proc".to_string(),
    ];

    if !policy.allow_network {
        args.push("--unshare-net".to_string());
    }

    // Note: bwrap does not support restricting process spawning (fork/exec) at the
    // namespace level. The allow_process_spawn policy field is enforced on macOS via
    // sandbox-exec profiles but has no bwrap equivalent. Seccomp filters would be
    // needed for this on Linux, which is out of scope for the bwrap wrapper.

    // Scrub host environment — only pass through explicitly requested env vars.
    args.push("--clearenv".to_string());

    for ro in &policy.read_only_paths {
        args.push("--ro-bind".to_string());
        args.push(ro.clone());
        args.push(ro.clone());
    }

    for rw in &policy.writable_paths {
        args.push("--bind".to_string());
        args.push(rw.clone());
        args.push(rw.clone());

        // Override sensitive subdirectories as read-only to prevent the
        // sandboxed process from corrupting git history or AVA config.
        // bwrap applies mounts in order, so later --ro-bind overrides
        // the parent --bind for these subtrees.
        for subdir in PROTECTED_SUBDIRS {
            let protected = format!("{}/{subdir}", rw.trim_end_matches('/'));
            if Path::new(&protected).is_dir() {
                args.push("--ro-bind".to_string());
                args.push(protected.clone());
                args.push(protected);
            }
        }
    }

    if let Some(cwd) = &request.working_dir {
        args.push("--chdir".to_string());
        args.push(cwd.clone());
    }

    // Set a safe default PATH so commands can still resolve
    args.push("--setenv".to_string());
    args.push("PATH".to_string());
    args.push("/usr/bin:/bin:/usr/sbin:/sbin".to_string());

    for (key, value) in &request.env {
        args.push("--setenv".to_string());
        args.push(key.clone());
        args.push(value.clone());
    }

    args.push("--".to_string());
    args.push(request.command.clone());
    args.extend(request.args.clone());

    Ok(SandboxPlan {
        program: "bwrap".to_string(),
        args,
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn bwrap_plan_has_unshare_flags() {
        let request = SandboxRequest {
            command: "echo".to_string(),
            args: vec!["hi".to_string()],
            working_dir: None,
            env: vec![],
        };
        let plan = build_bwrap_plan(&request, &SandboxPolicy::default()).unwrap();
        assert_eq!(plan.program, "bwrap");
        assert!(plan.args.contains(&"--unshare-user".to_string()));
    }

    #[test]
    fn bwrap_plan_includes_working_dir_and_env() {
        let request = SandboxRequest {
            command: "echo".to_string(),
            args: vec!["hi".to_string()],
            working_dir: Some("/tmp".to_string()),
            env: vec![("A".to_string(), "1".to_string())],
        };
        let plan = build_bwrap_plan(&request, &SandboxPolicy::default()).unwrap();

        assert!(plan.args.windows(2).any(|w| w == ["--chdir", "/tmp"]));
        assert!(plan.args.windows(3).any(|w| w == ["--setenv", "A", "1"]));
    }

    #[test]
    fn bwrap_plan_protects_git_and_ava_dirs() {
        // Use a real directory that has .git (the repo root).
        let repo_root = env!("CARGO_MANIFEST_DIR")
            .strip_suffix("/crates/ava-sandbox")
            .unwrap_or(env!("CARGO_MANIFEST_DIR"));

        // Only run the assertion if the repo root actually has .git
        let git_dir = format!("{repo_root}/.git");
        if !Path::new(&git_dir).is_dir() {
            return;
        }

        let request = SandboxRequest {
            command: "echo".to_string(),
            args: vec!["hi".to_string()],
            working_dir: None,
            env: vec![],
        };
        let policy = SandboxPolicy {
            writable_paths: vec![repo_root.to_string()],
            ..SandboxPolicy::default()
        };
        let plan = build_bwrap_plan(&request, &policy).unwrap();

        // The writable bind should appear first
        assert!(plan
            .args
            .windows(3)
            .any(|w| w[0] == "--bind" && w[1] == repo_root && w[2] == repo_root));

        // .git should be overridden as read-only
        assert!(
            plan.args
                .windows(3)
                .any(|w| w[0] == "--ro-bind" && w[1] == git_dir && w[2] == git_dir),
            "expected --ro-bind for .git: {:?}",
            plan.args
        );
    }

    #[test]
    fn bwrap_plan_skips_nonexistent_protected_dirs() {
        let request = SandboxRequest {
            command: "echo".to_string(),
            args: vec!["hi".to_string()],
            working_dir: None,
            env: vec![],
        };
        // /tmp typically has no .git or .ava
        let policy = SandboxPolicy {
            writable_paths: vec!["/tmp".to_string()],
            ..SandboxPolicy::default()
        };
        let plan = build_bwrap_plan(&request, &policy).unwrap();

        // Should not have any --ro-bind for /tmp/.git or /tmp/.ava
        assert!(
            !plan
                .args
                .windows(3)
                .any(|w| w[0] == "--ro-bind" && w[1].starts_with("/tmp/.")),
            "should not add --ro-bind for nonexistent dirs"
        );
    }
}
