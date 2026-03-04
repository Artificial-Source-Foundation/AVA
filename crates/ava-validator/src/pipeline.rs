use crate::{ValidationResult, Validator, DEFAULT_MAX_ATTEMPTS};
use std::borrow::Cow;

/// Produces optional content fixes after a validation failure.
pub trait FixGenerator {
    /// Returns updated content for another attempt, or `None` to stop retrying.
    fn generate_fix(
        &self,
        content: &str,
        failure: &ValidationResult,
        attempt: usize,
    ) -> Option<String>;
}

/// Ordered validator pipeline that stops at the first failure.
#[derive(Default)]
pub struct ValidationPipeline {
    validators: Vec<Box<dyn Validator>>,
}

impl ValidationPipeline {
    /// Creates an empty validation pipeline.
    #[must_use]
    pub fn new() -> Self {
        Self {
            validators: Vec::new(),
        }
    }

    /// Appends a validator and returns the updated pipeline.
    #[must_use]
    pub fn with_validator<V>(mut self, validator: V) -> Self
    where
        V: Validator + 'static,
    {
        self.validators.push(Box::new(validator));
        self
    }

    /// Runs validators in order and returns the first failure, if any.
    #[must_use]
    pub fn validate(&self, content: &str) -> ValidationResult {
        for validator in &self.validators {
            let result = validator.validate(content);
            if !result.valid {
                return result.prepend_detail(format!("validator '{}' failed", validator.name()));
            }
        }

        ValidationResult::valid()
    }
}

/// Final state returned by retry-based validation.
pub struct RetryOutcome {
    /// Validation result from the final attempt.
    pub result: ValidationResult,
    /// Content produced by the final attempt.
    pub final_content: String,
    /// Number of attempts that were executed.
    pub attempts: usize,
}

/// Validates content with bounded retries and optional automated fixes.
#[must_use]
pub fn validate_with_retry(
    pipeline: &ValidationPipeline,
    content: &str,
    fixer: &dyn FixGenerator,
    max_attempts: usize,
) -> RetryOutcome {
    let bounded_attempts = max_attempts.clamp(1, DEFAULT_MAX_ATTEMPTS);
    let mut current = Cow::Borrowed(content);

    for attempt in 1..=bounded_attempts {
        let result = pipeline.validate(current.as_ref());
        if result.valid {
            return RetryOutcome {
                result,
                final_content: current.into_owned(),
                attempts: attempt,
            };
        }

        if attempt == bounded_attempts {
            return RetryOutcome {
                result,
                final_content: current.into_owned(),
                attempts: attempt,
            };
        }

        match fixer.generate_fix(current.as_ref(), &result, attempt) {
            Some(updated) => current = Cow::Owned(updated),
            None => {
                return RetryOutcome {
                    result,
                    final_content: current.into_owned(),
                    attempts: attempt,
                }
            }
        }
    }

    RetryOutcome {
        result: ValidationResult::invalid(
            "validation retry exhausted",
            vec!["validation attempts exhausted unexpectedly".to_string()],
        ),
        final_content: current.into_owned(),
        attempts: bounded_attempts,
    }
}
