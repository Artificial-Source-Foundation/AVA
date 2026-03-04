use ava_permissions::{Action, Pattern, PermissionSystem, Rule};
use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, PartialEq, Eq, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum PermissionActionInput {
    Allow,
    Deny,
    Ask,
}

#[derive(Debug, Clone, PartialEq, Eq, Deserialize)]
#[serde(tag = "type", rename_all = "snake_case")]
pub enum PermissionPatternInput {
    Any,
    Glob { value: String },
    Regex { value: String },
    Path { value: String },
}

#[derive(Debug, Clone, PartialEq, Eq, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct PermissionRuleInput {
    pub tool: PermissionPatternInput,
    pub args: PermissionPatternInput,
    pub action: PermissionActionInput,
}

#[derive(Debug, Clone, PartialEq, Eq, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct EvaluatePermissionInput {
    pub workspace_root: String,
    pub rules: Vec<PermissionRuleInput>,
    pub tool: String,
    pub args: Vec<String>,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum PermissionActionOutput {
    Allow,
    Deny,
    Ask,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct EvaluatePermissionOutput {
    pub action: PermissionActionOutput,
}

impl From<PermissionPatternInput> for Pattern {
    fn from(value: PermissionPatternInput) -> Self {
        match value {
            PermissionPatternInput::Any => Pattern::Any,
            PermissionPatternInput::Glob { value } => Pattern::Glob(value),
            PermissionPatternInput::Regex { value } => Pattern::Regex(value),
            PermissionPatternInput::Path { value } => Pattern::Path(value),
        }
    }
}

impl From<PermissionActionInput> for Action {
    fn from(value: PermissionActionInput) -> Self {
        match value {
            PermissionActionInput::Allow => Action::Allow,
            PermissionActionInput::Deny => Action::Deny,
            PermissionActionInput::Ask => Action::Ask,
        }
    }
}

impl From<Action> for PermissionActionOutput {
    fn from(value: Action) -> Self {
        match value {
            Action::Allow => PermissionActionOutput::Allow,
            Action::Deny => PermissionActionOutput::Deny,
            Action::Ask => PermissionActionOutput::Ask,
        }
    }
}

#[tauri::command]
pub fn evaluate_permission(
    input: EvaluatePermissionInput,
) -> Result<EvaluatePermissionOutput, String> {
    let rules: Vec<Rule> = input
        .rules
        .into_iter()
        .map(|rule| Rule {
            tool: rule.tool.into(),
            args: rule.args.into(),
            action: rule.action.into(),
        })
        .collect();

    let system = PermissionSystem::load(input.workspace_root, rules);
    let args_refs: Vec<&str> = input.args.iter().map(String::as_str).collect();
    let action = system.evaluate(&input.tool, &args_refs);
    Ok(EvaluatePermissionOutput {
        action: action.into(),
    })
}

#[cfg(test)]
mod tests {
    use super::{evaluate_permission, EvaluatePermissionInput};
    use serde_json::json;

    #[test]
    fn evaluate_permission_maps_json_and_serializes_output() {
        let input: EvaluatePermissionInput = serde_json::from_value(json!({
            "workspaceRoot": "/workspace",
            "rules": [{
                "tool": { "type": "glob", "value": "bash" },
                "args": { "type": "any" },
                "action": "allow"
            }],
            "tool": "bash",
            "args": ["ls -la"]
        }))
        .expect("input json should deserialize");

        let output = evaluate_permission(input).expect("permission evaluation should succeed");
        let json_value = serde_json::to_value(&output).expect("output should serialize");

        assert_eq!(json_value["action"], "allow");
    }
}
