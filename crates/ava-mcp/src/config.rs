use std::collections::HashMap;
use std::path::Path;

use ava_types::{AvaError, Result};
use serde::{Deserialize, Serialize};

// ---------------------------------------------------------------------------
// MCP server configuration
// ---------------------------------------------------------------------------

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
    Http { url: String },
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

    let contents = tokio::fs::read_to_string(path)
        .await
        .map_err(|e| AvaError::IoError(format!("failed to read MCP config at {}: {e}", path.display())))?;

    let config: MCPConfigFile = serde_json::from_str(&contents)
        .map_err(|e| AvaError::SerializationError(format!("invalid MCP config: {e}")))?;

    Ok(config.servers)
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
            TransportType::Http { url } => {
                assert_eq!(url, "http://localhost:8080");
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

    #[test]
    fn roundtrip_serialization() {
        let config = MCPConfigFile {
            servers: vec![MCPServerConfig {
                name: "test".to_string(),
                transport: TransportType::Stdio {
                    command: "echo".to_string(),
                    args: vec!["hello".to_string()],
                    env: HashMap::new(),
                },
                enabled: true,
            }],
        };

        let json = serde_json::to_string(&config).unwrap();
        let parsed: MCPConfigFile = serde_json::from_str(&json).unwrap();
        assert_eq!(parsed.servers.len(), 1);
        assert_eq!(parsed.servers[0].name, "test");
    }
}
