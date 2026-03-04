use serde::{Deserialize, Serialize};

use ava_agent::{ErrorKind, ReflectionAgent, ReflectionLoop, ToolExecutor, ToolResult};

#[derive(Debug, Clone, PartialEq, Eq, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ToolResultInput {
    pub output: String,
    pub error: Option<String>,
}

#[derive(Debug, Clone, PartialEq, Eq, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ReflectAndFixInput {
    pub result: ToolResultInput,
    pub generated_fix: Option<String>,
    pub execution_result: Option<ToolResultInput>,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum ErrorKindOutput {
    Syntax,
    Import,
    Type,
    Command,
    Unknown,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct ReflectAndFixOutput {
    pub output: String,
    pub error: Option<String>,
    pub attempted_fix: bool,
    pub error_kind: Option<ErrorKindOutput>,
}

impl From<ToolResultInput> for ToolResult {
    fn from(value: ToolResultInput) -> Self {
        Self {
            output: value.output,
            error: value.error,
        }
    }
}

impl From<ToolResult> for ToolResultInput {
    fn from(value: ToolResult) -> Self {
        Self {
            output: value.output,
            error: value.error,
        }
    }
}

impl From<ErrorKind> for ErrorKindOutput {
    fn from(value: ErrorKind) -> Self {
        match value {
            ErrorKind::Syntax => ErrorKindOutput::Syntax,
            ErrorKind::Import => ErrorKindOutput::Import,
            ErrorKind::Type => ErrorKindOutput::Type,
            ErrorKind::Command => ErrorKindOutput::Command,
            ErrorKind::Unknown => ErrorKindOutput::Unknown,
        }
    }
}

struct ReflectionAgentAdapter {
    generated_fix: Option<String>,
}

impl ReflectionAgent for ReflectionAgentAdapter {
    fn generate_fix(&self, _error_kind: ErrorKind, _result: &ToolResult) -> Result<String, String> {
        self.generated_fix
            .clone()
            .ok_or_else(|| "no fix generated".to_string())
    }
}

struct ToolExecutorAdapter {
    execution_result: Option<ToolResult>,
}

impl ToolExecutor for ToolExecutorAdapter {
    fn execute_tool(&self, _input: &str) -> ToolResult {
        self.execution_result.clone().unwrap_or_else(|| ToolResult {
            output: String::new(),
            error: Some("execution result was not provided".to_string()),
        })
    }
}

#[tauri::command]
pub fn reflection_reflect_and_fix(
    input: ReflectAndFixInput,
) -> Result<ReflectAndFixOutput, String> {
    let error_kind = input
        .result
        .error
        .as_deref()
        .map(|error| ReflectionLoop::analyze_error(error).unwrap_or(ErrorKind::Unknown));
    let attempted_fix = input.result.error.is_some() && input.generated_fix.is_some();

    let reflection_agent = ReflectionAgentAdapter {
        generated_fix: input.generated_fix,
    };
    let tool_executor = ToolExecutorAdapter {
        execution_result: input.execution_result.map(ToolResult::from),
    };
    let reflection_loop = ReflectionLoop::new(&reflection_agent, &tool_executor);
    let final_result = reflection_loop.reflect_and_fix(ToolResult::from(input.result));
    let final_result_output = ToolResultInput::from(final_result);

    Ok(ReflectAndFixOutput {
        output: final_result_output.output,
        error: final_result_output.error,
        attempted_fix,
        error_kind: error_kind.map(ErrorKindOutput::from),
    })
}

#[cfg(test)]
mod tests {
    use super::{reflection_reflect_and_fix, ReflectAndFixInput};
    use serde_json::json;

    #[test]
    fn reflect_and_fix_maps_input_and_serializes_output() {
        let input: ReflectAndFixInput = serde_json::from_value(json!({
            "result": {
                "output": "",
                "error": "SyntaxError: unexpected token"
            },
            "generatedFix": "fixed code",
            "executionResult": {
                "output": "ok",
                "error": null
            }
        }))
        .expect("reflection input should deserialize");

        let output = reflection_reflect_and_fix(input).expect("reflect_and_fix should succeed");
        let output_json =
            serde_json::to_value(&output).expect("reflection output should serialize");

        assert_eq!(output_json["attemptedFix"], true);
        assert_eq!(output_json["errorKind"], "syntax");
        assert_eq!(output_json["output"], "ok");
        assert!(output_json["error"].is_null());
    }
}
