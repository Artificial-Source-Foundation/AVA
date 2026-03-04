use crate::error::{Result, SandboxError};
use crate::types::{SandboxPolicy, SandboxRequest};

pub fn validate_policy(policy: &SandboxPolicy) -> Result<()> {
    if policy.writable_paths.is_empty() {
        return Err(SandboxError::InvalidPolicy(
            "at least one writable path is required".to_string(),
        ));
    }
    Ok(())
}

pub fn validate_request(request: &SandboxRequest) -> Result<()> {
    if request.command.trim().is_empty() {
        return Err(SandboxError::InvalidPolicy(
            "command cannot be empty".to_string(),
        ));
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn rejects_empty_command() {
        let req = SandboxRequest {
            command: "".to_string(),
            args: vec![],
            working_dir: None,
            env: vec![],
        };
        assert!(validate_request(&req).is_err());
    }
}
