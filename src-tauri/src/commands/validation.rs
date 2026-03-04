use serde::{Deserialize, Serialize};

use ava_validator::{
    validate_with_retry, CompilationValidator, FixGenerator, RetryOutcome, SyntaxValidator,
    ValidationPipeline, ValidationResult,
};

#[derive(Debug, Clone, PartialEq, Eq, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ValidateEditInput {
    pub content: String,
}

#[derive(Debug, Clone, PartialEq, Eq, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ValidateWithRetryInput {
    pub content: String,
    pub max_attempts: usize,
    pub candidate_fixes: Vec<String>,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct ValidationResultOutput {
    pub valid: bool,
    pub error: Option<String>,
    pub details: Vec<String>,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct ValidateWithRetryOutput {
    pub result: ValidationResultOutput,
    pub final_content: String,
    pub attempts: usize,
}

#[tauri::command]
pub fn validation_validate_edit(
    input: ValidateEditInput,
) -> Result<ValidationResultOutput, String> {
    let pipeline = build_pipeline();
    Ok(map_result(pipeline.validate(&input.content)))
}

#[tauri::command]
pub fn validation_validate_with_retry(
    input: ValidateWithRetryInput,
) -> Result<ValidateWithRetryOutput, String> {
    let pipeline = build_pipeline();
    let fixer = CandidateFixGenerator {
        candidate_fixes: input.candidate_fixes,
    };
    let outcome = validate_with_retry(&pipeline, &input.content, &fixer, input.max_attempts);
    Ok(map_retry_outcome(outcome))
}

#[derive(Debug, Clone)]
struct CandidateFixGenerator {
    candidate_fixes: Vec<String>,
}

impl FixGenerator for CandidateFixGenerator {
    fn generate_fix(
        &self,
        _content: &str,
        _failure: &ValidationResult,
        attempt: usize,
    ) -> Option<String> {
        self.candidate_fixes.get(attempt - 1).cloned()
    }
}

fn build_pipeline() -> ValidationPipeline {
    ValidationPipeline::new()
        .with_validator(SyntaxValidator)
        .with_validator(CompilationValidator)
}

fn map_result(result: ValidationResult) -> ValidationResultOutput {
    ValidationResultOutput {
        valid: result.valid,
        error: result.error,
        details: result.details,
    }
}

fn map_retry_outcome(outcome: RetryOutcome) -> ValidateWithRetryOutput {
    ValidateWithRetryOutput {
        result: map_result(outcome.result),
        final_content: outcome.final_content,
        attempts: outcome.attempts,
    }
}

#[cfg(test)]
mod tests {
    use super::{
        validation_validate_edit, validation_validate_with_retry, ValidateEditInput,
        ValidateWithRetryInput,
    };
    use serde_json::json;

    #[test]
    fn validate_edit_returns_stable_output_shape() {
        let input: ValidateEditInput = serde_json::from_value(json!({
            "content": "fn broken( {"
        }))
        .expect("validate_edit input should deserialize");

        let output = validation_validate_edit(input).expect("validate_edit should succeed");
        let output_json =
            serde_json::to_value(&output).expect("validate_edit output should serialize");

        assert_eq!(output_json["valid"], false);
        assert_eq!(output_json["error"], "syntax validation failed");
        assert!(output_json["details"].is_array());
    }

    #[test]
    fn validate_with_retry_returns_stable_output_shape() {
        let input: ValidateWithRetryInput = serde_json::from_value(json!({
            "content": "fn broken( {",
            "maxAttempts": 3,
            "candidateFixes": ["fn fixed() {}"]
        }))
        .expect("validate_with_retry input should deserialize");

        let output =
            validation_validate_with_retry(input).expect("validate_with_retry should succeed");
        let output_json =
            serde_json::to_value(&output).expect("validate_with_retry output should serialize");

        assert_eq!(output_json["result"]["valid"], true);
        assert_eq!(output_json["finalContent"], "fn fixed() {}");
        assert_eq!(output_json["attempts"], 2);
    }
}
