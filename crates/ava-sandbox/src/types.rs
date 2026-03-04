#[derive(Debug, Clone, PartialEq, Eq)]
pub struct SandboxPolicy {
    pub read_only_paths: Vec<String>,
    pub writable_paths: Vec<String>,
    pub allow_network: bool,
    pub allow_process_spawn: bool,
}

impl Default for SandboxPolicy {
    fn default() -> Self {
        Self {
            read_only_paths: vec!["/usr".to_string(), "/bin".to_string()],
            writable_paths: vec!["/tmp".to_string()],
            allow_network: false,
            allow_process_spawn: false,
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct SandboxRequest {
    pub command: String,
    pub args: Vec<String>,
    pub working_dir: Option<String>,
    pub env: Vec<(String, String)>,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct SandboxPlan {
    pub program: String,
    pub args: Vec<String>,
}
