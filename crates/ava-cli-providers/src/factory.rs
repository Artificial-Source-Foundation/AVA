//! CLIProviderFactory — bridges CLI agent discovery with ModelRouter routing.

use std::collections::HashMap;

use ava_llm::router::ProviderFactory;
use ava_types::{AvaError, Result};

use crate::config::CLIAgentConfig;
use crate::discovery::{discover_agents, DiscoveredAgent};
use crate::provider::CLIAgentLLMProvider;

/// Factory that creates `CLIAgentLLMProvider` instances for discovered CLI agents.
///
/// Register with `ModelRouter::register_factory()` so that routes like
/// `provider="cli", model="claude-code"` are handled by the appropriate CLI agent.
pub struct CLIProviderFactory {
    configs: HashMap<String, CLIAgentConfig>,
    yolo: bool,
}

impl CLIProviderFactory {
    /// Create a factory from a set of discovered agents.
    pub fn from_discovered(agents: &[DiscoveredAgent], yolo: bool) -> Self {
        let configs = agents
            .iter()
            .map(|a| (a.name.clone(), a.config.clone()))
            .collect();
        Self { configs, yolo }
    }

    /// List the agent names this factory can create providers for.
    pub fn available_agents(&self) -> Vec<&str> {
        self.configs.keys().map(String::as_str).collect()
    }
}

impl ProviderFactory for CLIProviderFactory {
    fn handles(&self, provider_name: &str) -> bool {
        provider_name == "cli"
    }

    fn create(
        &self,
        _provider_name: &str,
        model: &str,
    ) -> Result<Box<dyn ava_llm::provider::LLMProvider>> {
        let config = self.configs.get(model).ok_or_else(|| {
            AvaError::ConfigError(format!(
                "CLI agent '{}' not found. Available: {:?}",
                model,
                self.configs.keys().collect::<Vec<_>>()
            ))
        })?;
        Ok(Box::new(CLIAgentLLMProvider::new(
            config.clone(),
            None,
            self.yolo,
        )))
    }
}

/// Discover installed CLI agents and create a factory for routing.
///
/// Returns both the discovered agents (for UI display) and the factory (for ModelRouter).
pub async fn discover_and_create_factory(yolo: bool) -> (Vec<DiscoveredAgent>, CLIProviderFactory) {
    let agents = discover_agents().await;
    let factory = CLIProviderFactory::from_discovered(&agents, yolo);
    (agents, factory)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::config::PromptMode;

    fn fake_agent(name: &str) -> DiscoveredAgent {
        DiscoveredAgent {
            name: name.to_string(),
            binary: name.to_string(),
            version: "1.0.0".to_string(),
            config: CLIAgentConfig {
                name: name.to_string(),
                binary: name.to_string(),
                prompt_flag: PromptMode::Flag("-p".to_string()),
                version_command: vec![name.to_string(), "--version".to_string()],
                ..Default::default()
            },
        }
    }

    #[test]
    fn factory_handles_cli_prefix() {
        let factory = CLIProviderFactory::from_discovered(&[], false);
        assert!(factory.handles("cli"));
        assert!(!factory.handles("anthropic"));
        assert!(!factory.handles("cli:claude-code"));
    }

    #[test]
    fn factory_creates_provider_for_known_agent() {
        let agents = vec![fake_agent("claude-code"), fake_agent("codex")];
        let factory = CLIProviderFactory::from_discovered(&agents, false);

        let provider = factory.create("cli", "claude-code");
        assert!(provider.is_ok());
    }

    #[test]
    fn factory_errors_for_unknown_agent() {
        let factory = CLIProviderFactory::from_discovered(&[fake_agent("codex")], false);
        let result = factory.create("cli", "unknown-agent");
        assert!(result.is_err());
    }

    #[test]
    fn available_agents_lists_known() {
        let agents = vec![fake_agent("claude-code"), fake_agent("aider")];
        let factory = CLIProviderFactory::from_discovered(&agents, false);
        let mut available = factory.available_agents();
        available.sort();
        assert_eq!(available, vec!["aider", "claude-code"]);
    }
}
