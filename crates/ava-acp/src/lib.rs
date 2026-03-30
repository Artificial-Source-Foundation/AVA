//! Agent Client Protocol (ACP) — Agent SDK integration and transport.
//!
//! This crate provides:
//! - Protocol types for the Anthropic Agent SDK streaming format
//! - An `AgentTransport` trait for bidirectional agent communication
//! - A stdio transport for spawning agent subprocesses (NDJSON over stdin/stdout)
//! - Adapters for Claude Agent SDK, legacy CLI agents, and ACP servers
//!
//! Replaces the old `ava-cli-providers` crate with a proper protocol-based approach.

pub mod adapters;
pub mod factory;
pub mod mapper;
pub mod protocol;
pub mod provider;
pub mod server;
pub mod stdio;
pub mod transport;

pub use factory::{transport_for_builtin_agent, AcpProviderFactory};
pub use mapper::{attach_delegation_record, ExternalRunDescriptor, ExternalSessionMapper};
pub use protocol::*;
pub use transport::*;

/// Info about a discovered external agent (for display in UI).
#[derive(Debug, Clone)]
pub struct DiscoveredAgent {
    pub name: String,
    pub binary: String,
    pub version: String,
}

/// Discover CLI agents installed on the system PATH.
///
/// Checks each built-in agent config's binary and version command.
/// Runs all checks concurrently with a per-agent timeout.
pub async fn discover_cli_agents() -> Vec<DiscoveredAgent> {
    use adapters::config::builtin_agents;
    use tokio::process::Command;

    let configs = builtin_agents();
    let mut handles = Vec::with_capacity(configs.len());

    for config in configs {
        if config.version_command.is_empty() {
            continue;
        }
        handles.push(tokio::spawn(async move {
            let binary = &config.version_command[0];
            let args = &config.version_command[1..];
            let output = tokio::time::timeout(
                std::time::Duration::from_secs(5),
                Command::new(binary).args(args).output(),
            )
            .await
            .ok()?
            .ok()?;

            if !output.status.success() {
                return None;
            }

            let raw = String::from_utf8_lossy(&output.stdout);
            let version = extract_version(&raw).unwrap_or_else(|| "unknown".into());

            Some(DiscoveredAgent {
                name: config.name,
                binary: config.binary,
                version,
            })
        }));
    }

    let mut agents = Vec::new();
    for handle in handles {
        if let Ok(Some(agent)) = handle.await {
            agents.push(agent);
        }
    }
    agents
}

/// Extract a semver-like version string from command output.
///
/// Finds patterns like "1.0.0", "v2.3.4", "0.15.2-beta" in the output.
fn extract_version(output: &str) -> Option<String> {
    let trimmed = output.trim();
    for (i, c) in trimmed.char_indices() {
        if c.is_ascii_digit() {
            let rest = &trimmed[i..];
            if let Some(dot_pos) = rest.find('.') {
                if dot_pos > 0 {
                    let end = rest
                        .find(|ch: char| ch.is_whitespace())
                        .unwrap_or(rest.len());
                    let candidate = &rest[..end];
                    if candidate.split('.').count() >= 2 {
                        return Some(candidate.to_string());
                    }
                }
            }
        }
    }
    None
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn extract_version_semver() {
        assert_eq!(extract_version("1.0.0"), Some("1.0.0".into()));
        assert_eq!(extract_version("v2.3.4"), Some("2.3.4".into()));
        assert_eq!(
            extract_version("claude-code version 1.2.3"),
            Some("1.2.3".into())
        );
    }

    #[test]
    fn extract_version_with_suffix() {
        assert_eq!(
            extract_version("0.15.2-beta.1"),
            Some("0.15.2-beta.1".into())
        );
    }

    #[test]
    fn extract_version_two_part() {
        assert_eq!(extract_version("gemini 3.0"), Some("3.0".into()));
    }

    #[test]
    fn extract_version_none_for_no_version() {
        assert_eq!(extract_version("no version here"), None);
        assert_eq!(extract_version(""), None);
    }

    #[tokio::test]
    async fn discover_finds_installed_agents() {
        let agents = discover_cli_agents().await;
        // We can't assert specific agents are installed, but we can check
        // that the function runs without panicking and returns valid data.
        for agent in &agents {
            assert!(!agent.name.is_empty());
            assert!(!agent.binary.is_empty());
            assert!(!agent.version.is_empty());
        }
    }
}
