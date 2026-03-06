pub mod bridge;
pub mod config;
pub mod configs;
pub mod discovery;
pub mod provider;
pub mod runner;

pub use bridge::{execute_with_cli_agent, AgentRole};
pub use config::{CLIAgentConfig, CLIAgentEvent, CLIAgentResult, PromptMode, TokenUsage};
pub use configs::builtin_configs;
pub use discovery::{create_providers, discover_agents, DiscoveredAgent};
pub use provider::CLIAgentLLMProvider;
pub use runner::{CLIAgentRunner, RunOptions};
