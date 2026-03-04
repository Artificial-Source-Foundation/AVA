mod pipeline;
mod validators;

/// Validation pipeline primitives and retry orchestration.
pub use pipeline::{validate_with_retry, FixGenerator, RetryOutcome, ValidationPipeline};
/// Built-in validators and their result contract.
pub use validators::{CompilationValidator, SyntaxValidator, ValidationResult, Validator};

/// Upper bound used by retry validation when larger values are requested.
pub const DEFAULT_MAX_ATTEMPTS: usize = 3;
