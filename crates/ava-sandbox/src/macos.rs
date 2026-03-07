use crate::error::Result;
use crate::policy::{validate_policy, validate_request};
use crate::types::{SandboxPlan, SandboxPolicy, SandboxRequest};

pub fn build_sandbox_exec_plan(
    request: &SandboxRequest,
    policy: &SandboxPolicy,
) -> Result<SandboxPlan> {
    validate_policy(policy)?;
    validate_request(request)?;

    let mut profile_parts = vec![
        "(version 1)".to_string(),
        "(deny default)".to_string(),
        "(allow process-exec)".to_string(),
        "(allow process-fork)".to_string(),
    ];

    for path in policy
        .read_only_paths
        .iter()
        .chain(policy.writable_paths.iter())
    {
        profile_parts.push(format!("(allow file-read* (subpath \"{path}\"))"));
    }

    for path in &policy.writable_paths {
        profile_parts.push(format!("(allow file-write* (subpath \"{path}\"))"));
    }

    if policy.allow_network {
        profile_parts.push("(allow network*)".to_string());
    }

    if let Some(cwd) = &request.working_dir {
        profile_parts.push(format!("(allow file-read* (subpath \"{cwd}\"))"));
        profile_parts.push(format!("(allow file-write* (subpath \"{cwd}\"))"));
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
}
