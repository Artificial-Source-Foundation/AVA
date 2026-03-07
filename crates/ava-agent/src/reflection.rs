/// Tool execution output consumed by the reflection loop.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ToolResult {
    /// Tool output payload.
    pub output: String,
    /// Optional tool error message.
    pub error: Option<String>,
}

/// Error categories used to select fix strategies.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ErrorKind {
    /// Parsing or syntax-level error.
    Syntax,
    /// Missing or unresolved import/module error.
    Import,
    /// Type mismatch or type inference error.
    Type,
    /// Command invocation or shell lookup error.
    Command,
    /// Error that does not match known categories.
    Unknown,
}

/// Contract for generating candidate fixes from failed tool runs.
pub trait ReflectionAgent {
    /// Returns a fix command/snippet for the error kind or an error if none is available.
    fn generate_fix(&self, error_kind: ErrorKind, result: &ToolResult) -> Result<String, String>;
}

/// Contract for executing tool input produced by the reflection loop.
pub trait ToolExecutor {
    /// Executes a tool invocation and returns structured output.
    fn execute_tool(&self, input: &str) -> ToolResult;
}

/// Coordinates error classification, fix generation, and retry execution.
pub struct ReflectionLoop<'a> {
    reflection_agent: &'a dyn ReflectionAgent,
    tool_executor: &'a dyn ToolExecutor,
}

const SYNTAX_PATTERNS: &[&str] = &["syntaxerror", "syntax error", "unexpected token"];
const IMPORT_PATTERNS: &[&str] = &[
    "cannot find module",
    "module not found",
    "no module named",
    "unresolved import",
    "importerror",
];
const TYPE_PATTERNS: &[&str] = &[
    "typeerror",
    "type error",
    "mismatched types",
    "is not assignable",
];
const COMMAND_PATTERNS: &[&str] = &[
    "command not found",
    "is not recognized as an internal or external command",
    "not found: ",
];

impl<'a> ReflectionLoop<'a> {
    /// Creates a reflection loop backed by an agent and executor.
    pub fn new(
        reflection_agent: &'a dyn ReflectionAgent,
        tool_executor: &'a dyn ToolExecutor,
    ) -> Self {
        Self {
            reflection_agent,
            tool_executor,
        }
    }

    /// Attempts to classify an error message into a known error kind.
    pub fn analyze_error(error: &str) -> Option<ErrorKind> {
        if contains_any_ascii_case_insensitive(error, SYNTAX_PATTERNS) {
            return Some(ErrorKind::Syntax);
        }

        if contains_any_ascii_case_insensitive(error, IMPORT_PATTERNS) {
            return Some(ErrorKind::Import);
        }

        if contains_any_ascii_case_insensitive(error, TYPE_PATTERNS) {
            return Some(ErrorKind::Type);
        }

        if contains_any_ascii_case_insensitive(error, COMMAND_PATTERNS) {
            return Some(ErrorKind::Command);
        }

        None
    }

    /// Runs one reflection cycle and executes a generated fix when possible.
    pub fn reflect_and_fix(&self, result: ToolResult) -> ToolResult {
        let Some(error_message) = result.error.as_deref() else {
            return result;
        };

        let error_kind = Self::analyze_error(error_message).unwrap_or(ErrorKind::Unknown);
        let Ok(fix) = self.reflection_agent.generate_fix(error_kind, &result) else {
            return result;
        };

        self.tool_executor.execute_tool(&fix)
    }
}

fn contains_any_ascii_case_insensitive(haystack: &str, needles: &[&str]) -> bool {
    if needles.is_empty() || haystack.is_empty() {
        return false;
    }

    let haystack_bytes = haystack.as_bytes();
    let max_len = haystack_bytes.len();

    for index in 0..max_len {
        let candidate = haystack_bytes[index].to_ascii_lowercase();

        for needle in needles {
            if needle.is_empty() {
                continue;
            }

            let needle_bytes = needle.as_bytes();
            let end = index + needle_bytes.len();
            if end > max_len {
                continue;
            }

            if candidate == needle_bytes[0].to_ascii_lowercase()
                && haystack_bytes[index..end].eq_ignore_ascii_case(needle_bytes)
            {
                return true;
            }
        }
    }

    false
}
