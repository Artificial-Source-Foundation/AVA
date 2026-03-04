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
        profile_parts.push(format!("(allow file-read* (subpath \"{}\"))", path));
    }

    for path in &policy.writable_paths {
        profile_parts.push(format!("(allow file-write* (subpath \"{}\"))", path));
    }

    if policy.allow_network {
        profile_parts.push("(allow network*)".to_string());
    }

    let profile = profile_parts.join(" ");

    let mut args = vec![
        "-p".to_string(),
        profile.to_string(),
        request.command.clone(),
    ];
    args.extend(request.args.clone());

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
}
