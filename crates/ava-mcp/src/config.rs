use std::collections::HashMap;
use std::path::Path;

use ava_types::{AvaError, Result};
use serde::{Deserialize, Serialize};

// ---------------------------------------------------------------------------
// OAuth configuration for remote MCP servers
// ---------------------------------------------------------------------------

/// OAuth authentication configuration for a remote MCP server.
///
/// Used when an MCP server requires OAuth 2.0 authentication (PKCE flow).
/// Tokens are stored in `~/.ava/credentials.json` under a key derived from
/// the server name (`mcp:{server_name}`).
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct McpOAuthConfig {
    /// OAuth 2.0 authorization endpoint.
    pub auth_url: String,
    /// OAuth 2.0 token endpoint.
    pub token_url: String,
    /// OAuth client ID registered with the authorization server.
    pub client_id: String,
    /// OAuth scopes to request (defaults to empty — server defines required scopes).
    #[serde(default)]
    pub scopes: Vec<String>,
    /// Local TCP port for the PKCE redirect callback listener (defaults to 9876).
    #[serde(default = "default_redirect_port")]
    pub redirect_port: u16,
}

fn default_redirect_port() -> u16 {
    9876
}

// ---------------------------------------------------------------------------
// MCP server configuration
// ---------------------------------------------------------------------------

/// Whether a server was loaded from the global (`~/.ava/mcp.json`) or local
/// (`.ava/mcp.json`) config file.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
pub enum McpServerScope {
    Global,
    Local,
}

impl std::fmt::Display for McpServerScope {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Global => write!(f, "global"),
            Self::Local => write!(f, "local"),
        }
    }
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct MCPServerConfig {
    pub name: String,
    pub transport: TransportType,
    #[serde(default = "default_enabled")]
    pub enabled: bool,
}

fn default_enabled() -> bool {
    true
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(tag = "type")]
pub enum TransportType {
    #[serde(rename = "stdio")]
    Stdio {
        command: String,
        #[serde(default)]
        args: Vec<String>,
        #[serde(default)]
        env: HashMap<String, String>,
    },
    #[serde(rename = "http")]
    Http {
        url: String,
        /// Optional OAuth configuration. When present, the HTTP transport will
        /// automatically perform the PKCE flow on first connect and on 401 responses.
        #[serde(default)]
        auth: Option<McpOAuthConfig>,
        /// Optional static Bearer token. Mutually exclusive with `auth`.
        /// Use for servers that issue long-lived API keys.
        #[serde(default)]
        bearer_token: Option<String>,
        /// HTTP headers to include on every request (e.g., custom API keys).
        #[serde(default)]
        headers: HashMap<String, String>,
    },
}

// ---------------------------------------------------------------------------
// Config file structure
// ---------------------------------------------------------------------------

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct MCPConfigFile {
    #[serde(default)]
    pub servers: Vec<MCPServerConfig>,
}

// ---------------------------------------------------------------------------
// Loading
// ---------------------------------------------------------------------------

/// Load MCP server configurations from a JSON file.
/// Returns an empty list if the file does not exist.
pub async fn load_mcp_config(path: &Path) -> Result<Vec<MCPServerConfig>> {
    if !path.exists() {
        return Ok(Vec::new());
    }

    let contents = tokio::fs::read_to_string(path).await.map_err(|e| {
        AvaError::IoError(format!(
            "failed to read MCP config at {}: {e}",
            path.display()
        ))
    })?;

    let config: MCPConfigFile = serde_json::from_str(&contents)
        .map_err(|e| AvaError::SerializationError(format!("invalid MCP config: {e}")))?;

    Ok(config.servers)
}

/// Load and merge MCP configs from global and project paths.
/// Project configs override global configs by server name.
pub async fn load_merged_mcp_config(global: &Path, project: &Path) -> Result<Vec<MCPServerConfig>> {
    let mut global_configs = load_mcp_config(global).await?;
    let project_configs = load_mcp_config(project).await?;

    if project_configs.is_empty() {
        return Ok(global_configs);
    }

    // Project overrides global by server name
    let project_names: std::collections::HashSet<&str> =
        project_configs.iter().map(|c| c.name.as_str()).collect();

    global_configs.retain(|c| !project_names.contains(c.name.as_str()));
    global_configs.extend(project_configs);
    Ok(global_configs)
}

/// Load and merge MCP configs, tagging each server with its scope (global vs local).
/// Project configs override global configs by server name.
pub async fn load_merged_mcp_config_with_scope(
    global: &Path,
    project: &Path,
) -> Result<Vec<(MCPServerConfig, McpServerScope)>> {
    let global_configs = load_mcp_config(global).await?;
    let project_configs = load_mcp_config(project).await?;

    let project_names: std::collections::HashSet<&str> =
        project_configs.iter().map(|c| c.name.as_str()).collect();

    let mut result: Vec<(MCPServerConfig, McpServerScope)> = global_configs
        .into_iter()
        .filter(|c| !project_names.contains(c.name.as_str()))
        .map(|c| (c, McpServerScope::Global))
        .collect();

    result.extend(
        project_configs
            .into_iter()
            .map(|c| (c, McpServerScope::Local)),
    );

    Ok(result)
}

/// Load MCP server configurations from a JSON string (useful for testing).
pub fn parse_mcp_config(json: &str) -> Result<Vec<MCPServerConfig>> {
    let config: MCPConfigFile =
        serde_json::from_str(json).map_err(|e| AvaError::SerializationError(e.to_string()))?;
    Ok(config.servers)
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parse_stdio_config() {
        let json = r#"{
            "servers": [
                {
                    "name": "filesystem",
                    "transport": {
                        "type": "stdio",
                        "command": "npx",
                        "args": ["-y", "@modelcontextprotocol/server-filesystem", "/tmp"]
                    },
                    "enabled": true
                }
            ]
        }"#;

        let configs = parse_mcp_config(json).unwrap();
        assert_eq!(configs.len(), 1);
        assert_eq!(configs[0].name, "filesystem");
        assert!(configs[0].enabled);
        match &configs[0].transport {
            TransportType::Stdio { command, args, env } => {
                assert_eq!(command, "npx");
                assert_eq!(args.len(), 3);
                assert!(env.is_empty());
            }
            _ => panic!("expected stdio transport"),
        }
    }

    #[test]
    fn parse_http_config() {
        let json = r#"{
            "servers": [
                {
                    "name": "remote",
                    "transport": {
                        "type": "http",
                        "url": "http://localhost:8080"
                    },
                    "enabled": true
                }
            ]
        }"#;

        let configs = parse_mcp_config(json).unwrap();
        assert_eq!(configs.len(), 1);
        match &configs[0].transport {
            TransportType::Http {
                url,
                auth,
                bearer_token,
                headers,
            } => {
                assert_eq!(url, "http://localhost:8080");
                assert!(auth.is_none());
                assert!(bearer_token.is_none());
                assert!(headers.is_empty());
            }
            _ => panic!("expected http transport"),
        }
    }

    #[test]
    fn parse_http_config_with_oauth() {
        let json = r#"{
            "servers": [
                {
                    "name": "slack",
                    "transport": {
                        "type": "http",
                        "url": "https://mcp.slack.com/api",
                        "auth": {
                            "auth_url": "https://slack.com/oauth/v2/authorize",
                            "token_url": "https://slack.com/api/oauth.v2.access",
                            "client_id": "my-client-id",
                            "scopes": ["channels:read", "chat:write"]
                        }
                    }
                }
            ]
        }"#;

        let configs = parse_mcp_config(json).unwrap();
        assert_eq!(configs.len(), 1);
        match &configs[0].transport {
            TransportType::Http { url, auth, .. } => {
                assert_eq!(url, "https://mcp.slack.com/api");
                let oauth = auth.as_ref().expect("expected oauth config");
                assert_eq!(oauth.client_id, "my-client-id");
                assert_eq!(oauth.scopes, vec!["channels:read", "chat:write"]);
                assert_eq!(oauth.redirect_port, 9876); // default
            }
            _ => panic!("expected http transport"),
        }
    }

    #[test]
    fn parse_http_config_with_bearer_token() {
        let json = r#"{
            "servers": [
                {
                    "name": "linear",
                    "transport": {
                        "type": "http",
                        "url": "https://mcp.linear.app/sse",
                        "bearer_token": "lin_api_abc123"
                    }
                }
            ]
        }"#;

        let configs = parse_mcp_config(json).unwrap();
        match &configs[0].transport {
            TransportType::Http {
                bearer_token, auth, ..
            } => {
                assert_eq!(bearer_token.as_deref(), Some("lin_api_abc123"));
                assert!(auth.is_none());
            }
            _ => panic!("expected http transport"),
        }
    }

    #[test]
    fn parse_mixed_config() {
        let json = r#"{
            "servers": [
                {
                    "name": "fs",
                    "transport": { "type": "stdio", "command": "fs-server" },
                    "enabled": true
                },
                {
                    "name": "disabled",
                    "transport": { "type": "stdio", "command": "nope" },
                    "enabled": false
                },
                {
                    "name": "web",
                    "transport": { "type": "http", "url": "http://example.com" }
                }
            ]
        }"#;

        let configs = parse_mcp_config(json).unwrap();
        assert_eq!(configs.len(), 3);
        assert!(configs[0].enabled);
        assert!(!configs[1].enabled);
        assert!(configs[2].enabled); // default true
                                     // http variant compiles correctly
        assert!(matches!(configs[2].transport, TransportType::Http { .. }));
    }

    #[test]
    fn parse_empty_config() {
        let json = r#"{"servers": []}"#;
        let configs = parse_mcp_config(json).unwrap();
        assert!(configs.is_empty());
    }

    #[test]
    fn parse_config_with_env() {
        let json = r#"{
            "servers": [{
                "name": "test",
                "transport": {
                    "type": "stdio",
                    "command": "test-server",
                    "args": ["--verbose"],
                    "env": { "API_KEY": "secret123" }
                }
            }]
        }"#;

        let configs = parse_mcp_config(json).unwrap();
        match &configs[0].transport {
            TransportType::Stdio { env, .. } => {
                assert_eq!(env.get("API_KEY").unwrap(), "secret123");
            }
            _ => panic!("expected stdio"),
        }
    }

    #[tokio::test]
    async fn load_nonexistent_file() {
        let configs = load_mcp_config(Path::new("/nonexistent/mcp.json"))
            .await
            .unwrap();
        assert!(configs.is_empty());
    }

    #[tokio::test]
    async fn merged_config_project_overrides_global() {
        let dir = tempfile::tempdir().unwrap();
        let global = dir.path().join("global-mcp.json");
        let project = dir.path().join("project-mcp.json");

        tokio::fs::write(
            &global,
            r#"{"servers": [
                {"name": "fs", "transport": {"type": "stdio", "command": "fs-server-v1"}},
                {"name": "git", "transport": {"type": "stdio", "command": "git-server"}}
            ]}"#,
        )
        .await
        .unwrap();

        tokio::fs::write(
            &project,
            r#"{"servers": [
                {"name": "fs", "transport": {"type": "stdio", "command": "fs-server-v2"}}
            ]}"#,
        )
        .await
        .unwrap();

        let configs = load_merged_mcp_config(&global, &project).await.unwrap();
        assert_eq!(configs.len(), 2);

        let fs_config = configs.iter().find(|c| c.name == "fs").unwrap();
        match &fs_config.transport {
            TransportType::Stdio { command, .. } => {
                assert_eq!(command, "fs-server-v2"); // project overrides global
            }
            _ => panic!("expected stdio"),
        }

        assert!(configs.iter().any(|c| c.name == "git")); // global-only server kept
    }

    #[tokio::test]
    async fn merged_config_empty_project_returns_global() {
        let dir = tempfile::tempdir().unwrap();
        let global = dir.path().join("global.json");
        let project = dir.path().join("nonexistent.json");

        tokio::fs::write(
            &global,
            r#"{"servers": [{"name": "test", "transport": {"type": "stdio", "command": "test"}}]}"#,
        )
        .await
        .unwrap();

        let configs = load_merged_mcp_config(&global, &project).await.unwrap();
        assert_eq!(configs.len(), 1);
        assert_eq!(configs[0].name, "test");
    }

    #[test]
    fn parse_invalid_json_returns_error() {
        let result = parse_mcp_config("not valid json {{{");
        assert!(result.is_err());
        let err = result.unwrap_err();
        match err {
            AvaError::SerializationError(_) => (),
            other => panic!("expected SerializationError, got: {other}"),
        }
    }

    #[test]
    fn roundtrip_serialization() {
        let config = MCPConfigFile {
            servers: vec![
                MCPServerConfig {
                    name: "test".to_string(),
                    transport: TransportType::Stdio {
                        command: "echo".to_string(),
                        args: vec!["hello".to_string()],
                        env: HashMap::new(),
                    },
                    enabled: true,
                },
                MCPServerConfig {
                    name: "remote".to_string(),
                    transport: TransportType::Http {
                        url: "https://example.com/mcp".to_string(),
                        auth: None,
                        bearer_token: Some("tok".to_string()),
                        headers: HashMap::new(),
                    },
                    enabled: true,
                },
            ],
        };

        let json = serde_json::to_string(&config).unwrap();
        let parsed: MCPConfigFile = serde_json::from_str(&json).unwrap();
        assert_eq!(parsed.servers.len(), 2);
        assert_eq!(parsed.servers[0].name, "test");
        assert_eq!(parsed.servers[1].name, "remote");
        match &parsed.servers[1].transport {
            TransportType::Http {
                url, bearer_token, ..
            } => {
                assert_eq!(url, "https://example.com/mcp");
                assert_eq!(bearer_token.as_deref(), Some("tok"));
            }
            _ => panic!("expected http"),
        }
    }
}
