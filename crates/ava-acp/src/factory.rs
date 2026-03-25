//! ACP provider factory for the model router.
//!
//! Handles `provider="acp"` and `provider="cli"` routes, creating
//! `AcpAgentProvider` instances on-demand from agent configs.

use std::collections::HashMap;

use ava_llm::provider::LLMProvider;
use ava_llm::router::ProviderFactory;
use ava_types::{AvaError, Result};

use crate::adapters::claude_sdk::ClaudeSdkAdapter;
use crate::adapters::config::{AgentConfig, AgentProtocol};
use crate::adapters::legacy_cli::LegacyCliAdapter;
use crate::provider::AcpAgentProvider;

/// Factory that creates ACP agent providers on-demand.
///
/// Registered with `ModelRouter` to handle `provider="acp"` and `provider="cli"` routes.
/// No eager discovery — agents are spawned only when requested.
pub struct AcpProviderFactory {
    configs: HashMap<String, AgentConfig>,
    yolo: bool,
}

impl AcpProviderFactory {
    /// Create a factory with the given agent configs.
    pub fn new(configs: Vec<AgentConfig>, yolo: bool) -> Self {
        let map = configs.into_iter().map(|c| (c.name.clone(), c)).collect();
        Self { configs: map, yolo }
    }

    /// Create a factory with the built-in agent configs.
    pub fn with_builtins(yolo: bool) -> Self {
        Self::new(crate::adapters::config::builtin_agents(), yolo)
    }

    /// List available agent names.
    pub fn available_agents(&self) -> Vec<&str> {
        self.configs.keys().map(|s| s.as_str()).collect()
    }
}

impl ProviderFactory for AcpProviderFactory {
    fn handles(&self, provider_name: &str) -> bool {
        provider_name == "acp" || provider_name == "cli"
    }

    fn create(&self, _provider_name: &str, model: &str) -> Result<Box<dyn LLMProvider>> {
        let config = self.configs.get(model).ok_or_else(|| {
            let available = self
                .configs
                .keys()
                .map(|s| s.as_str())
                .collect::<Vec<_>>()
                .join(", ");
            AvaError::ConfigError(format!(
                "Unknown ACP agent '{model}'. Available: {available}"
            ))
        })?;

        let transport: Box<dyn crate::transport::AgentTransport> = match config.protocol {
            AgentProtocol::SdkV1 => Box::new(ClaudeSdkAdapter::new(config.clone())),
            AgentProtocol::LegacyStreamJson | AgentProtocol::PlainText => {
                Box::new(LegacyCliAdapter::new(config.clone()))
            }
        };

        Ok(Box::new(AcpAgentProvider::new(
            transport,
            config.name.clone(),
            None,
            self.yolo,
        )))
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn factory_handles_acp_and_cli() {
        let factory = AcpProviderFactory::with_builtins(false);
        assert!(factory.handles("acp"));
        assert!(factory.handles("cli"));
        assert!(!factory.handles("openai"));
    }

    #[test]
    fn factory_creates_claude_code() {
        let factory = AcpProviderFactory::with_builtins(false);
        let provider = factory.create("acp", "claude-code").unwrap();
        assert_eq!(provider.model_name(), "claude-code");
    }

    #[test]
    fn factory_errors_on_unknown_agent() {
        let factory = AcpProviderFactory::with_builtins(false);
        let result = factory.create("acp", "nonexistent");
        assert!(result.is_err());
    }

    #[test]
    fn factory_lists_agents() {
        let factory = AcpProviderFactory::with_builtins(false);
        let agents = factory.available_agents();
        assert!(agents.contains(&"claude-code"));
        assert!(agents.contains(&"codex"));
        assert!(agents.contains(&"aider"));
    }
}
