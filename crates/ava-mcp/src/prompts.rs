//! MCP Prompts as Commands.
//!
//! Discovers prompt templates from connected MCP servers and provides
//! data structures and execution functions for wiring prompts into
//! the AVA slash command system.

use std::collections::HashMap;

use ava_types::{AvaError, Result};
use serde::{Deserialize, Serialize};

use crate::client::{MCPPrompt, MCPPromptArgument, MCPPromptContent};
use crate::manager::McpManager;

// ---------------------------------------------------------------------------
// McpPromptCommand — prompt template exposed as a command
// ---------------------------------------------------------------------------

/// An MCP prompt template exposed as a slash command.
///
/// Created during server connection when the server advertises prompt support.
/// These are wired into the AVA command system so users can invoke MCP prompts
/// via `/prompt:{server}:{name}` or similar patterns.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct McpPromptCommand {
    /// The MCP server that provides this prompt.
    pub server_name: String,
    /// The prompt template name (as registered on the server).
    pub prompt_name: String,
    /// Human-readable description of what the prompt does.
    pub description: String,
    /// Arguments accepted by this prompt template.
    pub arguments: Vec<McpPromptArg>,
}

/// An argument for an MCP prompt command.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct McpPromptArg {
    pub name: String,
    pub description: String,
    pub required: bool,
}

impl From<&MCPPromptArgument> for McpPromptArg {
    fn from(arg: &MCPPromptArgument) -> Self {
        Self {
            name: arg.name.clone(),
            description: arg.description.clone(),
            required: arg.required,
        }
    }
}

impl McpPromptCommand {
    /// Create a command from an MCP prompt and its server name.
    pub fn from_mcp_prompt(server_name: &str, prompt: &MCPPrompt) -> Self {
        Self {
            server_name: server_name.to_string(),
            prompt_name: prompt.name.clone(),
            description: prompt.description.clone(),
            arguments: prompt.arguments.iter().map(McpPromptArg::from).collect(),
        }
    }

    /// A display name for the command (e.g., "server:prompt_name").
    pub fn display_name(&self) -> String {
        format!("{}:{}", self.server_name, self.prompt_name)
    }
}

// ---------------------------------------------------------------------------
// Discovery & execution via McpManager
// ---------------------------------------------------------------------------

/// Discover all prompt commands from connected MCP servers.
///
/// Queries each connected server that advertises prompt support and returns
/// a list of `McpPromptCommand` structs ready for wiring into the command system.
pub async fn get_mcp_prompt_commands(manager: &McpManager) -> Vec<McpPromptCommand> {
    let mut commands = Vec::new();

    for server_name in manager.connected_server_names() {
        match manager.list_prompts(&server_name).await {
            Ok(prompts) => {
                for prompt in &prompts {
                    commands.push(McpPromptCommand::from_mcp_prompt(&server_name, prompt));
                }
            }
            Err(e) => {
                tracing::debug!(
                    server = %server_name,
                    error = %e,
                    "Failed to list prompts from MCP server"
                );
            }
        }
    }

    commands
}

/// Execute an MCP prompt on a specific server and return the rendered text.
///
/// Calls `prompts/get` on the server with the provided arguments and
/// concatenates the resulting message texts into a single string.
pub async fn execute_mcp_prompt(
    manager: &McpManager,
    server: &str,
    prompt: &str,
    args: HashMap<String, String>,
) -> Result<String> {
    let arguments = serde_json::to_value(args).map_err(|e| {
        AvaError::SerializationError(format!("Failed to serialize prompt args: {e}"))
    })?;

    let result = manager.get_prompt(server, prompt, arguments).await?;

    // Concatenate all message texts
    let text = result
        .messages
        .iter()
        .filter_map(|msg| match &msg.content {
            MCPPromptContent::Text { text } => Some(text.as_str()),
            MCPPromptContent::Resource { resource } => resource.text.as_deref(),
            MCPPromptContent::Unknown => None,
        })
        .collect::<Vec<_>>()
        .join("\n");

    Ok(text)
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

#[cfg(test)]
mod tests {
    use super::*;
    use crate::client::MCPPrompt;

    #[test]
    fn prompt_command_from_mcp_prompt() {
        let mcp_prompt = MCPPrompt {
            name: "code_review".to_string(),
            description: "Review code for quality issues".to_string(),
            arguments: vec![
                MCPPromptArgument {
                    name: "file".to_string(),
                    description: "Path to file to review".to_string(),
                    required: true,
                },
                MCPPromptArgument {
                    name: "style".to_string(),
                    description: "Review style".to_string(),
                    required: false,
                },
            ],
        };

        let cmd = McpPromptCommand::from_mcp_prompt("github", &mcp_prompt);
        assert_eq!(cmd.server_name, "github");
        assert_eq!(cmd.prompt_name, "code_review");
        assert_eq!(cmd.description, "Review code for quality issues");
        assert_eq!(cmd.arguments.len(), 2);
        assert_eq!(cmd.arguments[0].name, "file");
        assert!(cmd.arguments[0].required);
        assert_eq!(cmd.arguments[1].name, "style");
        assert!(!cmd.arguments[1].required);
    }

    #[test]
    fn prompt_command_display_name() {
        let cmd = McpPromptCommand {
            server_name: "slack".to_string(),
            prompt_name: "draft_message".to_string(),
            description: "Draft a Slack message".to_string(),
            arguments: vec![],
        };
        assert_eq!(cmd.display_name(), "slack:draft_message");
    }

    #[test]
    fn prompt_command_serialization() {
        let cmd = McpPromptCommand {
            server_name: "test".to_string(),
            prompt_name: "hello".to_string(),
            description: "Say hello".to_string(),
            arguments: vec![McpPromptArg {
                name: "name".to_string(),
                description: "Who to greet".to_string(),
                required: true,
            }],
        };

        let json = serde_json::to_string(&cmd).unwrap();
        let loaded: McpPromptCommand = serde_json::from_str(&json).unwrap();
        assert_eq!(loaded.server_name, "test");
        assert_eq!(loaded.prompt_name, "hello");
        assert_eq!(loaded.arguments.len(), 1);
    }

    #[tokio::test]
    async fn get_mcp_prompt_commands_empty_manager() {
        let manager = McpManager::new();
        let commands = get_mcp_prompt_commands(&manager).await;
        assert!(commands.is_empty());
    }
}
