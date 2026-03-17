//! OS-level sandbox planning for command execution.

pub mod error;
pub mod executor;
pub mod linux;
pub mod macos;
pub mod network_policy;
pub mod policy;
pub mod types;

pub use error::{Result, SandboxError};
pub use executor::{execute_plan, SandboxOutput};
pub use types::{SandboxPlan, SandboxPolicy, SandboxRequest};

pub trait SandboxBackend: Send + Sync {
    fn name(&self) -> &'static str;
    fn build_plan(&self, request: &SandboxRequest, policy: &SandboxPolicy) -> Result<SandboxPlan>;
}

pub struct LinuxSandbox;
pub struct MacOsSandbox;

impl SandboxBackend for LinuxSandbox {
    fn name(&self) -> &'static str {
        "linux-bwrap"
    }

    fn build_plan(&self, request: &SandboxRequest, policy: &SandboxPolicy) -> Result<SandboxPlan> {
        linux::build_bwrap_plan(request, policy)
    }
}

impl SandboxBackend for MacOsSandbox {
    fn name(&self) -> &'static str {
        "macos-sandbox-exec"
    }

    fn build_plan(&self, request: &SandboxRequest, policy: &SandboxPolicy) -> Result<SandboxPlan> {
        macos::build_sandbox_exec_plan(request, policy)
    }
}

pub fn select_backend() -> Result<Box<dyn SandboxBackend>> {
    #[cfg(target_os = "linux")]
    {
        Ok(Box::new(LinuxSandbox))
    }
    #[cfg(target_os = "macos")]
    {
        Ok(Box::new(MacOsSandbox))
    }
    #[cfg(not(any(target_os = "linux", target_os = "macos")))]
    {
        Err(SandboxError::UnsupportedPlatform(
            std::env::consts::OS.to_string(),
        ))
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn selected_backend_builds_plan() {
        let backend = select_backend().unwrap();
        let request = SandboxRequest {
            command: "echo".to_string(),
            args: vec!["ok".to_string()],
            working_dir: None,
            env: vec![],
        };
        let policy = SandboxPolicy::default();
        let plan = backend.build_plan(&request, &policy).unwrap();
        assert!(!plan.program.is_empty());
    }
}
