use std::path::Path;

use crate::error::{Result, SandboxError};
use crate::policy::{validate_policy, validate_request};
use crate::types::{SandboxPlan, SandboxPolicy, SandboxRequest};

/// Directories within writable mounts that should be read-only to prevent
/// the sandboxed process from corrupting git history or AVA config/credentials.
const PROTECTED_SUBDIRS: &[&str] = &[".git", ".ava"];

/// Escape a path string for safe embedding in an SBPL string literal.
///
/// Rejects paths containing characters that are syntactically meaningful in SBPL
/// (double quotes, backslashes, parentheses) to prevent profile injection.
fn escape_sbpl_path(path: &str) -> Result<String> {
    if path.contains('"') || path.contains('\\') || path.contains('(') || path.contains(')') {
        return Err(SandboxError::InvalidPolicy(format!(
            "path contains characters not allowed in SBPL literals: {path:?}"
        )));
    }
    Ok(path.to_string())
}

pub fn build_sandbox_exec_plan(
    request: &SandboxRequest,
    policy: &SandboxPolicy,
) -> Result<SandboxPlan> {
    validate_policy(policy)?;
    validate_request(request)?;

    let mut profile_parts = vec!["(version 1)".to_string(), "(deny default)".to_string()];

    if policy.allow_process_spawn {
        profile_parts.push("(allow process-exec)".to_string());
        profile_parts.push("(allow process-fork)".to_string());
    }
    // When allow_process_spawn is false, process-exec and process-fork remain
    // denied by the "(deny default)" rule above.

    for path in policy
        .read_only_paths
        .iter()
        .chain(policy.writable_paths.iter())
    {
        let safe = escape_sbpl_path(path)?;
        profile_parts.push(format!("(allow file-read* (subpath \"{safe}\"))"));
    }

    // Deny writes to protected subdirectories before allowing writes to
    // their parent. SBPL uses first-match-wins, so these deny rules must
    // appear before the broader file-write* allows.
    for path in &policy.writable_paths {
        for subdir in PROTECTED_SUBDIRS {
            let protected = format!("{}/{subdir}", path.trim_end_matches('/'));
            if Path::new(&protected).is_dir() {
                let safe = escape_sbpl_path(&protected)?;
                profile_parts.push(format!("(deny file-write* (subpath \"{safe}\"))"));
            }
        }
    }

    for path in &policy.writable_paths {
        let safe = escape_sbpl_path(path)?;
        profile_parts.push(format!("(allow file-write* (subpath \"{safe}\"))"));
    }

    if policy.allow_network {
        profile_parts.push("(allow network*)".to_string());
    }

    if let Some(cwd) = &request.working_dir {
        // Deny writes to protected subdirectories before allowing cwd writes.
        for subdir in PROTECTED_SUBDIRS {
            let protected = format!("{}/{subdir}", cwd.trim_end_matches('/'));
            if Path::new(&protected).is_dir() {
                let safe = escape_sbpl_path(&protected)?;
                profile_parts.push(format!("(deny file-write* (subpath \"{safe}\"))"));
            }
        }
        let safe = escape_sbpl_path(cwd)?;
        profile_parts.push(format!("(allow file-read* (subpath \"{safe}\"))"));
        profile_parts.push(format!("(allow file-write* (subpath \"{safe}\"))"));
    }

    let profile = profile_parts.join(" ");

    let mut args = vec!["-p".to_string(), profile.clone()];

    if request.env.is_empty() {
        args.push(request.command.clone());
        args.extend(request.args.clone());
    } else {
        args.push("/usr/bin/env".to_string());
        args.extend(request.env.iter().map(|(k, v)| format!("{k}={v}")));
        args.push(request.command.clone());
        args.extend(request.args.clone());
    }

    Ok(SandboxPlan {
        program: "sandbox-exec".to_string(),
        args,
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn sandbox_exec_plan_has_profile() {
        let request = SandboxRequest {
            command: "echo".to_string(),
            args: vec!["hi".to_string()],
            working_dir: None,
            env: vec![],
        };
        let plan = build_sandbox_exec_plan(&request, &SandboxPolicy::default()).unwrap();
        assert_eq!(plan.program, "sandbox-exec");
        assert_eq!(plan.args[0], "-p");
        assert!(plan.args[1].contains("deny default"));
    }

    #[test]
    fn sbpl_path_injection_is_rejected() {
        let malicious_cwd = "/tmp/evil\")(allow file-read* (subpath \"/etc\")".to_string();
        let request = SandboxRequest {
            command: "echo".to_string(),
            args: vec![],
            working_dir: Some(malicious_cwd),
            env: vec![],
        };
        let result = build_sandbox_exec_plan(&request, &SandboxPolicy::default());
        assert!(result.is_err(), "malicious path should be rejected");
        let err = result.unwrap_err().to_string();
        assert!(
            err.contains("invalid policy"),
            "error should indicate invalid policy: {err}"
        );
    }

    #[test]
    fn sandbox_exec_plan_includes_working_dir_and_env() {
        let request = SandboxRequest {
            command: "echo".to_string(),
            args: vec!["hi".to_string()],
            working_dir: Some("/tmp".to_string()),
            env: vec![("A".to_string(), "1".to_string())],
        };
        let plan = build_sandbox_exec_plan(&request, &SandboxPolicy::default()).unwrap();

        assert!(plan.args[1].contains("subpath \"/tmp\""));
        assert!(plan.args.iter().any(|arg| arg == "A=1"));
    }

    #[test]
    fn sbpl_profile_protects_git_and_ava_dirs() {
        let repo_root = env!("CARGO_MANIFEST_DIR")
            .strip_suffix("/crates/ava-sandbox")
            .unwrap_or(env!("CARGO_MANIFEST_DIR"));

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
        let plan = build_sandbox_exec_plan(&request, &policy).unwrap();
        let profile = &plan.args[1];

        // Deny for .git should appear before allow for the parent
        let deny_pos = profile
            .find(&format!("(deny file-write* (subpath \"{git_dir}\"))"))
            .expect("profile should deny writes to .git");
        let allow_pos = profile
            .find(&format!("(allow file-write* (subpath \"{repo_root}\"))"))
            .expect("profile should allow writes to repo root");
        assert!(
            deny_pos < allow_pos,
            "deny for .git must appear before allow for parent (deny@{deny_pos}, allow@{allow_pos})"
        );
    }

    #[test]
    fn sbpl_profile_skips_nonexistent_protected_dirs() {
        let request = SandboxRequest {
            command: "echo".to_string(),
            args: vec!["hi".to_string()],
            working_dir: None,
            env: vec![],
        };
        let policy = SandboxPolicy {
            writable_paths: vec!["/tmp".to_string()],
            ..SandboxPolicy::default()
        };
        let plan = build_sandbox_exec_plan(&request, &policy).unwrap();
        let profile = &plan.args[1];

        assert!(
            !profile.contains("deny file-write* (subpath \"/tmp/.git\")"),
            "should not deny nonexistent .git"
        );
        assert!(
            !profile.contains("deny file-write* (subpath \"/tmp/.ava\")"),
            "should not deny nonexistent .ava"
        );
    }
}
