//! Architect-editor two-phase coding, inspired by Aider.
//!
//! An **architect** model plans changes in natural language (producing an
//! [`ArchitectPlan`]), then an **editor** model applies the edits by executing
//! the corresponding tool calls.
//!
//! This module provides:
//! - Data types: [`ArchitectPlan`], [`EditStep`], [`EditAction`]
//! - Parsing: [`parse_architect_plan`] — extracts a plan from free-form LLM output
//! - Conversion: [`plan_to_tool_calls`] — turns a plan into executable [`ToolCall`]s
//! - Prompt: [`ArchitectEditor::default_plan_prompt`] — system prompt for the architect phase

use ava_types::ToolCall;
use serde::{Deserialize, Serialize};
use uuid::Uuid;

// ---------------------------------------------------------------------------
// Data types
// ---------------------------------------------------------------------------

/// A structured plan produced by the architect model.
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct ArchitectPlan {
    /// High-level description of what the plan achieves.
    pub description: String,
    /// Ordered list of individual edit steps.
    pub steps: Vec<EditStep>,
}

/// A single file-level edit within an [`ArchitectPlan`].
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct EditStep {
    /// Absolute or project-relative path to the target file.
    pub file_path: String,
    /// Whether to create, modify, or delete the file.
    pub action: EditAction,
    /// Natural-language description of the change.
    pub description: String,
    /// Text to locate inside the file (required for [`EditAction::Modify`]).
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub search_text: Option<String>,
    /// Replacement text (required for [`EditAction::Modify`]).
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub replacement_text: Option<String>,
    /// Full file content (required for [`EditAction::Create`]).
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub new_content: Option<String>,
}

/// The kind of file operation an [`EditStep`] represents.
#[derive(Debug, Clone, Copy, Serialize, Deserialize, PartialEq, Eq)]
#[serde(rename_all = "snake_case")]
pub enum EditAction {
    Create,
    Modify,
    Delete,
}

// ---------------------------------------------------------------------------
// Architect prompt
// ---------------------------------------------------------------------------

/// Orchestrator for the architect-editor two-phase coding pattern.
#[derive(Debug, Clone)]
pub struct ArchitectEditor {
    /// System prompt sent to the architect model when requesting a plan.
    pub plan_prompt_template: String,
}

impl Default for ArchitectEditor {
    fn default() -> Self {
        Self {
            plan_prompt_template: Self::default_plan_prompt().to_owned(),
        }
    }
}

impl ArchitectEditor {
    /// Returns the default system prompt for the architect phase.
    ///
    /// The prompt instructs the model to output a JSON object matching the
    /// [`ArchitectPlan`] schema, optionally wrapped in a markdown code block.
    pub fn default_plan_prompt() -> &'static str {
        r#"You are an architect that plans code changes. Analyze the request and produce a structured JSON plan.

Output a single JSON object with this schema:

{
  "description": "<high-level summary of all changes>",
  "steps": [
    {
      "file_path": "<path to file>",
      "action": "create" | "modify" | "delete",
      "description": "<what this step does>",
      "search_text": "<exact text to find — required for modify>",
      "replacement_text": "<replacement text — required for modify>",
      "new_content": "<full file content — required for create>"
    }
  ]
}

Rules:
- For "modify": provide exact `search_text` (the literal text currently in the file) and `replacement_text`.
- For "create": provide `new_content` with the full file contents.
- For "delete": only `file_path`, `action`, and `description` are needed.
- Order steps so that dependencies are satisfied (e.g., create a file before modifying it).
- Keep `search_text` minimal but unique within the file.
- You may wrap the JSON in a ```json code block."#
    }
}

// ---------------------------------------------------------------------------
// Parsing
// ---------------------------------------------------------------------------

/// Parse an [`ArchitectPlan`] from the architect model's response text.
///
/// The function tries, in order:
/// 1. Extract JSON from a fenced code block (` ```json ... ``` ` or ` ``` ... ``` `).
/// 2. Parse the entire response as JSON directly.
/// 3. Fallback: wrap the raw text in a single-step plan with a `Modify` action and
///    the full response as the description.
pub fn parse_architect_plan(response: &str) -> Result<ArchitectPlan, ArchitectParseError> {
    // Try extracting from a markdown code block first.
    if let Some(json_str) = extract_json_block(response) {
        if let Ok(plan) = serde_json::from_str::<ArchitectPlan>(json_str) {
            return Ok(plan);
        }
    }

    // Try parsing the whole response as JSON.
    if let Ok(plan) = serde_json::from_str::<ArchitectPlan>(response.trim()) {
        return Ok(plan);
    }

    // Fallback: treat the entire response as a free-form description.
    Ok(ArchitectPlan {
        description: "Plan parsed from unstructured response".to_string(),
        steps: vec![EditStep {
            file_path: String::new(),
            action: EditAction::Modify,
            description: response.trim().to_string(),
            search_text: None,
            replacement_text: None,
            new_content: None,
        }],
    })
}

/// Error type for architect plan parsing (currently only used for documentation;
/// the public API always falls back to a single-step plan).
#[derive(Debug, thiserror::Error)]
pub enum ArchitectParseError {
    #[error("JSON parse error: {0}")]
    Json(#[from] serde_json::Error),
}

/// Extract the contents of the first fenced code block from `text`.
fn extract_json_block(text: &str) -> Option<&str> {
    // Match ```json or ``` followed by content and closing ```.
    let start_markers = ["```json", "```"];
    for marker in &start_markers {
        if let Some(start_idx) = text.find(marker) {
            let content_start = start_idx + marker.len();
            // Skip optional newline after the opening fence.
            let content_start = if text[content_start..].starts_with('\n') {
                content_start + 1
            } else {
                content_start
            };
            if let Some(end_offset) = text[content_start..].find("```") {
                let block = text[content_start..content_start + end_offset].trim();
                if !block.is_empty() {
                    return Some(block);
                }
            }
        }
    }
    None
}

// ---------------------------------------------------------------------------
// Conversion to tool calls
// ---------------------------------------------------------------------------

/// Convert an [`ArchitectPlan`] into a list of [`ToolCall`]s that the agent
/// can execute sequentially.
///
/// Mapping:
/// - [`EditAction::Modify`] -> `edit` tool with `file_path`, `old_string`, `new_string`
/// - [`EditAction::Create`] -> `write` tool with `file_path`, `content`
/// - [`EditAction::Delete`] -> `bash` tool with `rm <file_path>`
pub fn plan_to_tool_calls(plan: &ArchitectPlan) -> Vec<ToolCall> {
    plan.steps
        .iter()
        .map(|step| {
            let id = Uuid::new_v4().to_string();
            match step.action {
                EditAction::Modify => {
                    let old_string = step.search_text.as_deref().unwrap_or_default();
                    let new_string = step.replacement_text.as_deref().unwrap_or_default();
                    ToolCall {
                        id,
                        name: "edit".to_string(),
                        arguments: serde_json::json!({
                            "file_path": step.file_path,
                            "old_string": old_string,
                            "new_string": new_string,
                        }),
                    }
                }
                EditAction::Create => {
                    let content = step.new_content.as_deref().unwrap_or_default();
                    ToolCall {
                        id,
                        name: "write".to_string(),
                        arguments: serde_json::json!({
                            "file_path": step.file_path,
                            "content": content,
                        }),
                    }
                }
                EditAction::Delete => ToolCall {
                    id,
                    name: "bash".to_string(),
                    arguments: serde_json::json!({
                        "command": format!("rm {}", shell_escape(&step.file_path)),
                    }),
                },
            }
        })
        .collect()
}

/// Minimal shell escaping: wraps the path in single quotes, escaping any
/// embedded single quotes.
fn shell_escape(s: &str) -> String {
    format!("'{}'", s.replace('\'', "'\\''"))
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

#[cfg(test)]
mod tests {
    use super::*;

    fn sample_plan_json() -> &'static str {
        r#"{
            "description": "Add a greeting function",
            "steps": [
                {
                    "file_path": "src/lib.rs",
                    "action": "modify",
                    "description": "Add greet function after imports",
                    "search_text": "use std::io;",
                    "replacement_text": "use std::io;\n\npub fn greet() -> &'static str {\n    \"hello\"\n}"
                },
                {
                    "file_path": "src/utils.rs",
                    "action": "create",
                    "description": "Create a new utility file",
                    "new_content": "pub fn helper() -> bool { true }"
                },
                {
                    "file_path": "src/old.rs",
                    "action": "delete",
                    "description": "Remove deprecated module"
                }
            ]
        }"#
    }

    #[test]
    fn parse_valid_plan() {
        let plan = parse_architect_plan(sample_plan_json()).unwrap();
        assert_eq!(plan.description, "Add a greeting function");
        assert_eq!(plan.steps.len(), 3);
        assert_eq!(plan.steps[0].action, EditAction::Modify);
        assert_eq!(plan.steps[0].file_path, "src/lib.rs");
        assert_eq!(plan.steps[0].search_text.as_deref(), Some("use std::io;"));
        assert_eq!(plan.steps[1].action, EditAction::Create);
        assert!(plan.steps[1].new_content.is_some());
        assert_eq!(plan.steps[2].action, EditAction::Delete);
    }

    #[test]
    fn parse_plan_in_markdown_block() {
        let response = format!(
            "Sure, here is the plan:\n\n```json\n{}\n```\n\nLet me know if you want changes.",
            sample_plan_json()
        );
        let plan = parse_architect_plan(&response).unwrap();
        assert_eq!(plan.description, "Add a greeting function");
        assert_eq!(plan.steps.len(), 3);
    }

    #[test]
    fn parse_plan_in_bare_code_block() {
        let response = format!("Here you go:\n\n```\n{}\n```", sample_plan_json());
        let plan = parse_architect_plan(&response).unwrap();
        assert_eq!(plan.steps.len(), 3);
    }

    #[test]
    fn fallback_for_unparseable_response() {
        let response = "I think you should change the function name from foo to bar in main.rs";
        let plan = parse_architect_plan(response).unwrap();
        assert_eq!(plan.steps.len(), 1);
        assert_eq!(plan.steps[0].action, EditAction::Modify);
        assert_eq!(plan.steps[0].description, response);
        assert!(plan.steps[0].file_path.is_empty());
    }

    #[test]
    fn plan_to_tool_calls_modify() {
        let plan = ArchitectPlan {
            description: "test".into(),
            steps: vec![EditStep {
                file_path: "src/lib.rs".into(),
                action: EditAction::Modify,
                description: "change foo to bar".into(),
                search_text: Some("foo".into()),
                replacement_text: Some("bar".into()),
                new_content: None,
            }],
        };
        let calls = plan_to_tool_calls(&plan);
        assert_eq!(calls.len(), 1);
        assert_eq!(calls[0].name, "edit");
        assert_eq!(calls[0].arguments["file_path"], "src/lib.rs");
        assert_eq!(calls[0].arguments["old_string"], "foo");
        assert_eq!(calls[0].arguments["new_string"], "bar");
    }

    #[test]
    fn plan_to_tool_calls_create() {
        let plan = ArchitectPlan {
            description: "test".into(),
            steps: vec![EditStep {
                file_path: "src/new.rs".into(),
                action: EditAction::Create,
                description: "create file".into(),
                search_text: None,
                replacement_text: None,
                new_content: Some("fn main() {}".into()),
            }],
        };
        let calls = plan_to_tool_calls(&plan);
        assert_eq!(calls.len(), 1);
        assert_eq!(calls[0].name, "write");
        assert_eq!(calls[0].arguments["file_path"], "src/new.rs");
        assert_eq!(calls[0].arguments["content"], "fn main() {}");
    }

    #[test]
    fn plan_to_tool_calls_delete() {
        let plan = ArchitectPlan {
            description: "test".into(),
            steps: vec![EditStep {
                file_path: "src/old.rs".into(),
                action: EditAction::Delete,
                description: "remove file".into(),
                search_text: None,
                replacement_text: None,
                new_content: None,
            }],
        };
        let calls = plan_to_tool_calls(&plan);
        assert_eq!(calls.len(), 1);
        assert_eq!(calls[0].name, "bash");
        assert!(calls[0].arguments["command"]
            .as_str()
            .unwrap()
            .contains("src/old.rs"));
    }

    #[test]
    fn plan_to_tool_calls_mixed() {
        let plan = parse_architect_plan(sample_plan_json()).unwrap();
        let calls = plan_to_tool_calls(&plan);
        assert_eq!(calls.len(), 3);
        assert_eq!(calls[0].name, "edit");
        assert_eq!(calls[1].name, "write");
        assert_eq!(calls[2].name, "bash");
    }

    #[test]
    fn shell_escape_handles_quotes() {
        let escaped = shell_escape("it's a file.rs");
        assert_eq!(escaped, "'it'\\''s a file.rs'");
    }

    #[test]
    fn roundtrip_serialization() {
        let plan = parse_architect_plan(sample_plan_json()).unwrap();
        let json = serde_json::to_string(&plan).unwrap();
        let plan2: ArchitectPlan = serde_json::from_str(&json).unwrap();
        assert_eq!(plan, plan2);
    }
}
