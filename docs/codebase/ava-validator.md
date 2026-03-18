# ava-validator

> Code validation pipeline with retry orchestration.

## Public API

| Type/Function | Description |
|--------------|-------------|
| `ValidationPipeline` | Ordered validator pipeline that stops at first failure |
| `ValidationPipeline::new()` | Create empty pipeline |
| `ValidationPipeline::with_validator()` | Add validator (builder pattern) |
| `ValidationPipeline::validate()` | Run all validators, return first failure or success |
| `Validator` | Trait: `name()` and `validate(content)` |
| `ValidationResult` | valid flag, optional error, details vector |
| `ValidationResult::valid()` | Create success result |
| `ValidationResult::invalid()` | Create failure result with error and details |
| `ValidationResult::prepend_detail()` | Add diagnostic prefix |
| `SyntaxValidator` | Checks merge markers and unbalanced delimiters |
| `CompilationValidator` | Checks for compile_error! and unresolved merge markers |
| `FixGenerator` | Trait for automated fix generation during retry |
| `FixGenerator::generate_fix()` | Return updated content or None to stop retrying |
| `validate_with_retry()` | Retry validation with bounded attempts and fix generation |
| `RetryOutcome` | Final result, content, and attempt count |
| `DEFAULT_MAX_ATTEMPTS` | Upper bound (3) for retry validation |

## Module Map

| File | Purpose |
|------|---------|
| `lib.rs` | Public exports and DEFAULT_MAX_ATTEMPTS constant |
| `pipeline.rs` | ValidationPipeline, FixGenerator, validate_with_retry |
| `validators.rs` | Validator trait, ValidationResult, SyntaxValidator, CompilationValidator |

## Dependencies

Uses: None (no internal AVA crate dependencies)

Used by:
- `src-tauri` — Desktop validation integration

## Key Patterns

- **Pipeline pattern**: Validators run sequentially; first failure short-circuits
- **Retry with fix generation**: Bounded attempts (max 3) with optional automated fixes
- **Builder pattern**: `with_validator()` returns self for chaining
- **Must-use annotations**: Prevents accidental ignoring of validation results
- **Delimiter tracking**: Stack-based bracket matching with byte positions
- **Conflict markers**: Detects Git merge markers (`<<<<<<<`, `=======`, `>>>>>>>`)
- **Rust-specific**: Checks for `compile_error!` macro and `<<<`/`>>>` markers
