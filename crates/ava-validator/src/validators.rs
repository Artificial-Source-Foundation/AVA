/// Standard output type for validator checks.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ValidationResult {
    /// Indicates whether validation succeeded.
    pub valid: bool,
    /// High-level failure message when validation fails.
    pub error: Option<String>,
    /// Detailed diagnostics collected during validation.
    pub details: Vec<String>,
}

impl ValidationResult {
    /// Creates a successful validation result.
    #[must_use]
    pub fn valid() -> Self {
        Self {
            valid: true,
            error: None,
            details: Vec::new(),
        }
    }

    /// Creates a failed validation result with details.
    #[must_use]
    pub fn invalid(error: impl Into<String>, details: Vec<String>) -> Self {
        Self {
            valid: false,
            error: Some(error.into()),
            details,
        }
    }

    /// Prepends one diagnostic detail while preserving existing entries.
    #[must_use]
    pub fn prepend_detail(mut self, detail: String) -> Self {
        let mut details = Vec::with_capacity(self.details.len() + 1);
        details.push(detail);
        details.extend(self.details);
        self.details = details;
        self
    }
}

/// Contract for pluggable content validators.
pub trait Validator: Send + Sync {
    /// Stable validator name used in diagnostics.
    fn name(&self) -> &'static str;
    /// Executes validation for the provided content.
    fn validate(&self, content: &str) -> ValidationResult;
}

/// Checks for merge markers and unbalanced delimiters.
#[derive(Debug, Default, Clone, Copy)]
pub struct SyntaxValidator;

impl Validator for SyntaxValidator {
    fn name(&self) -> &'static str {
        "syntax"
    }

    fn validate(&self, content: &str) -> ValidationResult {
        let mut stack: Vec<(char, usize)> = Vec::new();
        let mut details: Vec<String> = Vec::new();
        let mut line_start = 0;

        for line in content.split('\n') {
            if let Some(marker) = conflict_marker_at_line_start(line) {
                details.push(format!(
                    "found conflict marker '{marker}' at byte {line_start}"
                ));
            }

            for (offset, character) in line.char_indices() {
                let index = line_start + offset;

                if is_open_delimiter(character) {
                    stack.push((character, index));
                    continue;
                }

                if is_close_delimiter(character) {
                    let expected = matching_open_delimiter(character);
                    match stack.pop() {
                        Some((open, _)) if open == expected => {}
                        Some((open, open_index)) => {
                            details.push(format!(
                                "mismatched delimiters: opened '{open}' at byte {open_index}, closed '{character}' at byte {index}"
                            ));
                        }
                        None => {
                            details.push(format!(
                                "closing delimiter '{character}' at byte {index} has no opening delimiter"
                            ));
                        }
                    }
                }
            }

            line_start += line.len() + 1;
        }

        for (open, index) in stack {
            details.push(format!(
                "opening delimiter '{open}' at byte {index} is not closed"
            ));
        }

        if details.is_empty() {
            ValidationResult::valid()
        } else {
            ValidationResult::invalid("syntax validation failed", details)
        }
    }
}

/// Checks for obvious compile-time failure markers.
#[derive(Debug, Default, Clone, Copy)]
pub struct CompilationValidator;

impl Validator for CompilationValidator {
    fn name(&self) -> &'static str {
        "compilation"
    }

    fn validate(&self, content: &str) -> ValidationResult {
        let mut details: Vec<String> = Vec::new();

        if content.contains("compile_error!") {
            details.push("source contains compile_error! macro invocation".to_string());
        }

        if content.contains("<<<") || content.contains(">>>") {
            details.push("source contains unresolved merge markers".to_string());
        }

        if details.is_empty() {
            ValidationResult::valid()
        } else {
            ValidationResult::invalid("compilation validation failed", details)
        }
    }
}

fn conflict_marker_at_line_start(line: &str) -> Option<&'static str> {
    if line.starts_with("<<<<<<<") {
        return Some("<<<<<<<");
    }

    if line.starts_with("=======") {
        return Some("=======");
    }

    if line.starts_with(">>>>>>>") {
        return Some(">>>>>>>");
    }

    None
}

fn is_open_delimiter(character: char) -> bool {
    matches!(character, '(' | '{' | '[')
}

fn is_close_delimiter(character: char) -> bool {
    matches!(character, ')' | '}' | ']')
}

fn matching_open_delimiter(character: char) -> char {
    match character {
        ')' => '(',
        '}' => '{',
        ']' => '[',
        _ => character,
    }
}
