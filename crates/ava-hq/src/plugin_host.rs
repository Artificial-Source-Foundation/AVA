use crate::roles::{default_role_profiles, AgentRoleProfile};
use serde::{Deserialize, Serialize};
use serde_json::{json, Value};

const HQ_PLUGIN_NAME: &str = "hq";
const HQ_PLUGIN_VERSION: &str = env!("CARGO_PKG_VERSION");

#[derive(Debug, Clone, Default, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct PluginContext {
    #[serde(default)]
    pub project: PluginProjectContext,
}

#[derive(Debug, Clone, Default, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct PluginProjectContext {
    #[serde(default)]
    pub directory: String,
    #[serde(default)]
    pub name: String,
}

#[derive(Debug, Clone, Serialize, PartialEq, Eq)]
#[serde(rename_all = "camelCase")]
pub struct PluginCapabilities {
    pub app: PluginAppCapabilities,
}

#[derive(Debug, Clone, Serialize, PartialEq, Eq)]
#[serde(rename_all = "camelCase")]
pub struct PluginAppCapabilities {
    pub commands: Vec<PluginCommandSpec>,
    pub routes: Vec<PluginRouteSpec>,
    pub events: Vec<PluginEventSpec>,
    pub mounts: Vec<PluginMountSpec>,
}

#[derive(Debug, Clone, Serialize, PartialEq, Eq)]
#[serde(rename_all = "camelCase")]
pub struct PluginCommandSpec {
    pub name: String,
    pub description: String,
}

#[derive(Debug, Clone, Serialize, PartialEq, Eq)]
#[serde(rename_all = "camelCase")]
pub struct PluginRouteSpec {
    pub path: String,
    pub method: String,
    pub description: String,
}

#[derive(Debug, Clone, Serialize, PartialEq, Eq)]
#[serde(rename_all = "camelCase")]
pub struct PluginEventSpec {
    pub name: String,
    pub description: String,
}

#[derive(Debug, Clone, Serialize, PartialEq, Eq)]
#[serde(rename_all = "camelCase")]
pub struct PluginMountSpec {
    pub id: String,
    pub location: String,
    pub label: String,
    pub description: String,
}

#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct PluginAppResponse {
    pub result: Value,
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub emitted_events: Vec<PluginEvent>,
}

#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct PluginEvent {
    pub event: String,
    pub payload: Value,
}

#[derive(Debug, Clone, Serialize, PartialEq)]
#[serde(rename_all = "camelCase")]
pub struct RoleSummary {
    pub id: String,
    pub display_name: String,
    pub model: String,
    pub enabled: bool,
    pub allowed_tools: usize,
    pub allowed_mcp_servers: usize,
}

pub fn initialize_response() -> PluginCapabilities {
    PluginCapabilities {
        app: PluginAppCapabilities {
            commands: vec![
                PluginCommandSpec {
                    name: "hq.status".to_string(),
                    description: "Report HQ plugin status and available roles".to_string(),
                },
                PluginCommandSpec {
                    name: "hq.roles.list".to_string(),
                    description: "List HQ role profiles exposed by the plugin".to_string(),
                },
            ],
            routes: vec![
                PluginRouteSpec {
                    path: "/status".to_string(),
                    method: "GET".to_string(),
                    description: "Return HQ plugin status metadata".to_string(),
                },
                PluginRouteSpec {
                    path: "/roles".to_string(),
                    method: "GET".to_string(),
                    description: "Return HQ role profile summaries".to_string(),
                },
            ],
            events: vec![PluginEventSpec {
                name: "hq.status.requested".to_string(),
                description: "Emitted when the HQ plugin status command is invoked".to_string(),
            }],
            mounts: vec![
                PluginMountSpec {
                    id: "hq.dashboard".to_string(),
                    location: "sidebar.panel".to_string(),
                    label: "HQ Dashboard".to_string(),
                    description: "HQ dashboard mount owned by the HQ plugin".to_string(),
                },
                PluginMountSpec {
                    id: "hq.settings".to_string(),
                    location: "settings.section".to_string(),
                    label: "HQ Settings".to_string(),
                    description: "HQ settings mount owned by the HQ plugin".to_string(),
                },
            ],
        },
    }
}

pub fn handle_command(
    context: &PluginContext,
    command: &str,
    payload: Value,
) -> Result<PluginAppResponse, String> {
    match command {
        "hq.status" => Ok(PluginAppResponse {
            result: json!({
                "plugin": HQ_PLUGIN_NAME,
                "version": HQ_PLUGIN_VERSION,
                "project": {
                    "name": context.project.name.as_str(),
                    "directory": context.project.directory.as_str(),
                },
                "roles": role_summaries(),
                "payload": payload,
            }),
            emitted_events: vec![PluginEvent {
                event: "hq.status.requested".to_string(),
                payload: json!({
                    "project": context.project.name.as_str(),
                    "command": command,
                }),
            }],
        }),
        "hq.roles.list" => Ok(PluginAppResponse {
            result: json!({
                "roles": role_summaries(),
            }),
            emitted_events: Vec::new(),
        }),
        _ => Err(format!("unknown HQ plugin command '{command}'")),
    }
}

pub fn handle_route(
    context: &PluginContext,
    method: &str,
    path: &str,
    query: Value,
    _body: Option<Value>,
) -> Result<PluginAppResponse, String> {
    match (method, path) {
        ("GET", "/status") => Ok(PluginAppResponse {
            result: json!({
                "plugin": HQ_PLUGIN_NAME,
                "version": HQ_PLUGIN_VERSION,
                "project": {
                    "name": context.project.name.as_str(),
                    "directory": context.project.directory.as_str(),
                },
                "roleCount": role_summaries().len(),
                "query": query,
            }),
            emitted_events: Vec::new(),
        }),
        ("GET", "/roles") => Ok(PluginAppResponse {
            result: json!({
                "roles": role_summaries(),
                "query": query,
            }),
            emitted_events: Vec::new(),
        }),
        _ => Err(format!("unknown HQ plugin route '{method} {path}'")),
    }
}

pub fn role_summaries() -> Vec<RoleSummary> {
    let mut profiles: Vec<AgentRoleProfile> = default_role_profiles().into_values().collect();
    profiles.sort_by(|left, right| left.id.cmp(&right.id));
    profiles
        .into_iter()
        .map(|profile| RoleSummary {
            id: profile.id,
            display_name: profile.display_name,
            model: profile.model,
            enabled: profile.enabled,
            allowed_tools: profile.allowed_tools.len(),
            allowed_mcp_servers: profile.allowed_mcp_servers.len(),
        })
        .collect()
}

#[cfg(test)]
mod tests {
    use super::*;

    fn sample_context() -> PluginContext {
        PluginContext {
            project: PluginProjectContext {
                directory: "/tmp/estela".to_string(),
                name: "Estela".to_string(),
            },
        }
    }

    #[test]
    fn initialize_response_exposes_hq_mounts_and_commands() {
        let response = initialize_response();
        assert_eq!(response.app.commands.len(), 2);
        assert_eq!(response.app.routes.len(), 2);
        assert_eq!(response.app.mounts.len(), 2);
        assert!(response
            .app
            .mounts
            .iter()
            .any(|mount| mount.id == "hq.dashboard"));
        assert!(response
            .app
            .commands
            .iter()
            .any(|command| command.name == "hq.roles.list"));
    }

    #[test]
    fn status_command_returns_project_and_roles() {
        let response = handle_command(&sample_context(), "hq.status", json!({"source": "test"}))
            .expect("status command should succeed");
        let role_count = response
            .result
            .get("roles")
            .and_then(Value::as_array)
            .map(std::vec::Vec::len)
            .unwrap_or_default();
        assert!(role_count >= 4);
        assert_eq!(response.emitted_events.len(), 1);
        assert_eq!(response.result["project"]["name"], "Estela");
    }

    #[test]
    fn roles_route_returns_sorted_role_summaries() {
        let response = handle_route(&sample_context(), "GET", "/roles", Value::Null, None)
            .expect("roles route should succeed");
        let roles = response
            .result
            .get("roles")
            .and_then(Value::as_array)
            .expect("roles array should exist");
        assert!(roles.iter().any(|role| role["id"] == "director"));
        assert!(roles.iter().any(|role| role["id"] == "scout"));
    }
}
