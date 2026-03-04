use ava_validator::{
    validate_with_retry, CompilationValidator, FixGenerator, SyntaxValidator, ValidationPipeline,
    ValidationResult, Validator, DEFAULT_MAX_ATTEMPTS,
};
use std::sync::{
    atomic::{AtomicUsize, Ordering},
    Arc,
};

struct CountingInvalidValidator {
    calls: Arc<AtomicUsize>,
}

impl CountingInvalidValidator {
    fn new(calls: Arc<AtomicUsize>) -> Self {
        Self { calls }
    }
}

impl Validator for CountingInvalidValidator {
    fn name(&self) -> &'static str {
        "counting-invalid"
    }

    fn validate(&self, _content: &str) -> ValidationResult {
        self.calls.fetch_add(1, Ordering::SeqCst);
        ValidationResult::invalid("always invalid", vec!["forced failure".to_string()])
    }
}

struct CountingPassValidator {
    calls: Arc<AtomicUsize>,
}

impl CountingPassValidator {
    fn new(calls: Arc<AtomicUsize>) -> Self {
        Self { calls }
    }
}

impl Validator for CountingPassValidator {
    fn name(&self) -> &'static str {
        "counting-pass"
    }

    fn validate(&self, _content: &str) -> ValidationResult {
        self.calls.fetch_add(1, Ordering::SeqCst);
        ValidationResult::valid()
    }
}

struct MarkerFixer;

impl FixGenerator for MarkerFixer {
    fn generate_fix(
        &self,
        content: &str,
        _failure: &ValidationResult,
        _attempt: usize,
    ) -> Option<String> {
        Some(content.replace("compile_error!\"fail\"", "1 + 1"))
    }
}

struct RepeatFixer;

impl FixGenerator for RepeatFixer {
    fn generate_fix(
        &self,
        content: &str,
        _failure: &ValidationResult,
        _attempt: usize,
    ) -> Option<String> {
        Some(content.to_string())
    }
}

struct PanicFixer;

impl FixGenerator for PanicFixer {
    fn generate_fix(
        &self,
        _content: &str,
        _failure: &ValidationResult,
        _attempt: usize,
    ) -> Option<String> {
        panic!("fixer should not be called")
    }
}

struct TwoStepFixer {
    calls: Arc<AtomicUsize>,
}

impl TwoStepFixer {
    fn new(calls: Arc<AtomicUsize>) -> Self {
        Self { calls }
    }
}

impl FixGenerator for TwoStepFixer {
    fn generate_fix(
        &self,
        content: &str,
        _failure: &ValidationResult,
        _attempt: usize,
    ) -> Option<String> {
        let call = self.calls.fetch_add(1, Ordering::SeqCst);
        if call == 0 {
            Some(content.replace("compile_error!\"fail\"", "compile_error!\"still_fail\""))
        } else {
            None
        }
    }
}

#[test]
fn syntax_validator_passes_for_balanced_source() {
    let validator = SyntaxValidator;

    let result = validator.validate("fn main() { let x = (1 + 2); }\n");

    assert!(result.valid);
    assert!(result.error.is_none());
}

#[test]
fn syntax_validator_fails_for_unbalanced_delimiters() {
    let validator = SyntaxValidator;

    let result = validator.validate("fn main( {\n");

    assert!(!result.valid);
    assert_eq!(result.error.as_deref(), Some("syntax validation failed"));
    assert!(!result.details.is_empty());
}

#[test]
fn syntax_validator_detects_conflict_markers_only_at_line_boundaries() {
    let validator = SyntaxValidator;
    let source = "fn main() {\n    let inline = \"<<<<<<< not a marker\";\n<<<<<<< ours\nlet x = 1;\n=======\nlet x = 2;\n>>>>>>> theirs\n}\n";

    let result = validator.validate(source);

    assert!(!result.valid);
    let marker_details: Vec<&String> = result
        .details
        .iter()
        .filter(|detail| detail.starts_with("found conflict marker"))
        .collect();
    assert_eq!(marker_details.len(), 3);
}

#[test]
fn syntax_validator_reports_mismatched_delimiters_with_stable_details() {
    let validator = SyntaxValidator;

    let result = validator.validate("fn main() { ([)] }\n");

    assert!(!result.valid);
    assert_eq!(
        result.details,
        vec![
            "mismatched delimiters: opened '[' at byte 13, closed ')' at byte 14".to_string(),
            "mismatched delimiters: opened '(' at byte 12, closed ']' at byte 15".to_string()
        ]
    );
}

#[test]
fn compilation_validator_passes_without_compile_error_macro() {
    let validator = CompilationValidator;

    let result = validator.validate("fn main() { let _value = 1 + 1; }\n");

    assert!(result.valid);
    assert!(result.error.is_none());
}

#[test]
fn compilation_validator_fails_when_compile_error_macro_is_present() {
    let validator = CompilationValidator;

    let result = validator.validate("fn main() { compile_error!\"fail\"; }\n");

    assert!(!result.valid);
    assert_eq!(
        result.error.as_deref(),
        Some("compilation validation failed")
    );
    assert!(!result.details.is_empty());
}

#[test]
fn pipeline_short_circuits_on_first_invalid_result() {
    let first_calls = Arc::new(AtomicUsize::new(0));
    let second_calls = Arc::new(AtomicUsize::new(0));
    let first = CountingInvalidValidator::new(Arc::clone(&first_calls));
    let second = CountingPassValidator::new(Arc::clone(&second_calls));
    let pipeline = ValidationPipeline::new()
        .with_validator(first)
        .with_validator(second);

    let result = pipeline.validate("fn main() {}\n");

    assert!(!result.valid);
    assert_eq!(first_calls.load(Ordering::SeqCst), 1);
    assert_eq!(second_calls.load(Ordering::SeqCst), 0);
}

#[test]
fn retry_orchestration_succeeds_after_fixer_correction() {
    let pipeline = ValidationPipeline::new()
        .with_validator(SyntaxValidator)
        .with_validator(CompilationValidator);
    let fixer = MarkerFixer;

    let outcome = validate_with_retry(
        &pipeline,
        "fn main() { compile_error!\"fail\"; }\n",
        &fixer,
        DEFAULT_MAX_ATTEMPTS,
    );

    assert!(outcome.result.valid);
    assert_eq!(outcome.attempts, 2);
    assert!(outcome.final_content.contains("1 + 1"));
}

#[test]
fn retry_cap_reached_returns_final_failure() {
    let pipeline = ValidationPipeline::new()
        .with_validator(SyntaxValidator)
        .with_validator(CompilationValidator);
    let fixer = RepeatFixer;

    let outcome = validate_with_retry(
        &pipeline,
        "fn main() { compile_error!\"fail\"; }\n",
        &fixer,
        2,
    );

    assert!(!outcome.result.valid);
    assert_eq!(outcome.attempts, 2);
    assert_eq!(
        outcome.result.error.as_deref(),
        Some("compilation validation failed")
    );
}

#[test]
fn retry_returns_initial_content_on_first_attempt_success() {
    let pipeline = ValidationPipeline::new()
        .with_validator(SyntaxValidator)
        .with_validator(CompilationValidator);
    let fixer = PanicFixer;
    let source = "fn main() { let _value = (1 + 2); }\n";

    let outcome = validate_with_retry(&pipeline, source, &fixer, DEFAULT_MAX_ATTEMPTS);

    assert!(outcome.result.valid);
    assert_eq!(outcome.attempts, 1);
    assert_eq!(outcome.final_content, source);
}

#[test]
fn retry_returns_latest_content_and_attempts_when_fixer_stops() {
    let pipeline = ValidationPipeline::new()
        .with_validator(SyntaxValidator)
        .with_validator(CompilationValidator);
    let calls = Arc::new(AtomicUsize::new(0));
    let fixer = TwoStepFixer::new(Arc::clone(&calls));

    let outcome = validate_with_retry(
        &pipeline,
        "fn main() { compile_error!\"fail\"; }\n",
        &fixer,
        DEFAULT_MAX_ATTEMPTS,
    );

    assert!(!outcome.result.valid);
    assert_eq!(outcome.attempts, 2);
    assert_eq!(calls.load(Ordering::SeqCst), 2);
    assert_eq!(
        outcome.final_content,
        "fn main() { compile_error!\"still_fail\"; }\n"
    );
}
