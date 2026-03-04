use crate::error::Result;
use crate::policy::{validate_policy, validate_request};
use crate::types::{SandboxPlan, SandboxPolicy, SandboxRequest};

pub fn build_bwrap_plan(request: &SandboxRequest, policy: &SandboxPolicy) -> Result<SandboxPlan> {
    validate_policy(policy)?;
    validate_request(request)?;

    let mut args = vec![
        "--unshare-user".to_string(),
        "--unshare-pid".to_string(),
        "--die-with-parent".to_string(),
    ];

    if !policy.allow_network {
        args.push("--unshare-net".to_string());
    }

    for ro in &policy.read_only_paths {
        args.push("--ro-bind".to_string());
        args.push(ro.clone());
        args.push(ro.clone());
    }

    for rw in &policy.writable_paths {
        args.push("--bind".to_string());
        args.push(rw.clone());
        args.push(rw.clone());
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
}
