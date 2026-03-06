use crate::config::CLIAgentConfig;
use crate::configs::builtin_configs;
use crate::provider::CLIAgentLLMProvider;
use crate::runner::CLIAgentRunner;
use ava_llm::provider::LLMProvider;
use std::collections::HashMap;
use std::sync::Arc;

/// Discovered CLI agent with version info.
#[derive(Debug, Clone)]
pub struct DiscoveredAgent {
    pub name: String,
    pub binary: String,
    pub version: String,
    pub config: CLIAgentConfig,
}

/// Discover which CLI agents are installed on this system.
pub async fn discover_agents() -> Vec<DiscoveredAgent> {
    discover_agents_from_configs(builtin_configs()).await
}

pub(crate) async fn discover_agents_from_configs(
    configs: HashMap<String, CLIAgentConfig>,
) -> Vec<DiscoveredAgent> {
    let mut discovered = Vec::new();
    let mut handles = Vec::new();

    for (name, config) in configs {
        handles.push(tokio::spawn(async move {
            let runner = CLIAgentRunner::new(config.clone());
            if runner.is_available().await {
                let version = runner.version().await.unwrap_or_else(|| "unknown".into());
                Some(DiscoveredAgent {
                    name,
                    binary: config.binary.clone(),
                    version,
                    config,
                })
            } else {
                None
            }
        }));
    }

    for handle in handles {
        if let Ok(Some(agent)) = handle.await {
            discovered.push(agent);
        }
    }

    discovered.sort_by(|a, b| a.name.cmp(&b.name));
    discovered
}

/// Create LLMProvider instances from discovered agents.
pub fn create_providers(
    agents: &[DiscoveredAgent],
    yolo: bool,
) -> HashMap<String, Arc<dyn LLMProvider>> {
    let mut providers: HashMap<String, Arc<dyn LLMProvider>> = HashMap::new();

    for agent in agents {
        let provider_name = format!("cli:{}", agent.name);
        let provider = CLIAgentLLMProvider::new(agent.config.clone(), None, yolo);
        providers.insert(provider_name, Arc::new(provider));
    }

    providers
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::config::PromptMode;
    use std::time::Duration;

    fn fake_config(name: &str, binary: &str, version_cmd: Vec<String>) -> CLIAgentConfig {
        CLIAgentConfig {
            name: name.to_string(),
            binary: binary.to_string(),
            prompt_flag: PromptMode::Flag("-p".to_string()),
            non_interactive_flags: vec![],
            yolo_flags: vec![],
            output_format_flag: None,
            allowed_tools_flag: None,
            cwd_flag: None,
            model_flag: None,
            session_flag: None,
            supports_stream_json: false,
            supports_tool_scoping: false,
            tier_tool_scopes: None,
            version_command: version_cmd,
        }
    }

    #[tokio::test]
    async fn discover_returns_empty_when_binaries_missing() {
        let configs = HashMap::from([
            (
                "missing-a".to_string(),
                fake_config(
                    "missing-a",
                    "__missing_a__",
                    vec!["__missing_a__".to_string(), "--version".to_string()],
                ),
            ),
            (
                "missing-b".to_string(),
                fake_config(
                    "missing-b",
                    "__missing_b__",
                    vec!["__missing_b__".to_string(), "--version".to_string()],
                ),
            ),
        ]);

        let discovered = discover_agents_from_configs(configs).await;
        assert!(discovered.is_empty());
    }

    #[test]
    fn create_providers_prefixes_with_cli_namespace() {
        let agent = DiscoveredAgent {
            name: "codex".to_string(),
            binary: "codex".to_string(),
            version: "1.2.3".to_string(),
            config: fake_config(
                "codex",
                "codex",
                vec!["codex".to_string(), "--version".to_string()],
            ),
        };

        let providers = create_providers(&[agent], false);
        assert!(providers.contains_key("cli:codex"));
    }

    #[tokio::test]
    async fn discovery_runs_checks_in_parallel() {
        let configs = HashMap::from([
            (
                "a".to_string(),
                fake_config(
                    "a",
                    "sh",
                    vec!["sh".to_string(), "-c".to_string(), "sleep 0.2".to_string()],
                ),
            ),
            (
                "b".to_string(),
                fake_config(
                    "b",
                    "sh",
                    vec!["sh".to_string(), "-c".to_string(), "sleep 0.2".to_string()],
                ),
            ),
            (
                "c".to_string(),
                fake_config(
                    "c",
                    "sh",
                    vec!["sh".to_string(), "-c".to_string(), "sleep 0.2".to_string()],
                ),
            ),
        ]);

        let start = std::time::Instant::now();
        let _ = discover_agents_from_configs(configs).await;
        let elapsed = start.elapsed();
        assert!(elapsed < Duration::from_millis(450));
    }

    #[tokio::test]
    async fn unavailable_agents_are_excluded() {
        let configs = HashMap::from([
            (
                "available".to_string(),
                fake_config(
                    "available",
                    "sh",
                    vec!["sh".to_string(), "-c".to_string(), "echo ok".to_string()],
                ),
            ),
            (
                "missing".to_string(),
                fake_config(
                    "missing",
                    "__missing_binary__",
                    vec!["__missing_binary__".to_string(), "--version".to_string()],
                ),
            ),
        ]);

        let discovered = discover_agents_from_configs(configs).await;
        assert_eq!(discovered.len(), 1);
        assert_eq!(discovered[0].name, "available");
    }
}
