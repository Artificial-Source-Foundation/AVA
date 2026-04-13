//! Benchmark task definitions for model comparison.
//!
//! Each task has a prompt, expected output patterns for quality validation,
//! and a difficulty category. Tasks may include a test harness for compile-and-test
//! validation (Tier 2) or setup code for agentic editing tasks (Tier 3).

#[path = "benchmark_tasks/agent_quality_tasks.rs"]
mod agent_quality_tasks;
#[path = "benchmark_tasks/agentic_tasks.rs"]
mod agentic_tasks;
#[path = "benchmark_tasks/large_project_tasks.rs"]
mod large_project_tasks;
#[path = "benchmark_tasks/lsp_smoke_tasks.rs"]
mod lsp_smoke_tasks;
#[path = "benchmark_tasks/maintenance_tasks.rs"]
mod maintenance_tasks;
#[path = "benchmark_tasks/mcp_integration_tasks.rs"]
mod mcp_integration_tasks;
#[path = "benchmark_tasks/multi_language_tasks.rs"]
mod multi_language_tasks;
#[path = "benchmark_tasks/normal_coding_tasks.rs"]
mod normal_coding_tasks;
#[path = "benchmark_tasks/product_smoke_tasks.rs"]
mod product_smoke_tasks;
#[path = "benchmark_tasks/prompt_regression_tasks.rs"]
mod prompt_regression_tasks;
#[path = "benchmark_tasks/security_and_test_tasks.rs"]
mod security_and_test_tasks;
#[path = "benchmark_tasks/small_coding_tasks.rs"]
mod small_coding_tasks;
#[path = "benchmark_tasks/speed_tasks.rs"]
mod speed_tasks;
#[path = "benchmark_tasks/stress_coding_tasks.rs"]
mod stress_coding_tasks;
#[path = "benchmark_tasks/test_heavy_tasks.rs"]
mod test_heavy_tasks;
#[path = "benchmark_tasks/tool_recovery_tasks.rs"]
mod tool_recovery_tasks;
#[path = "benchmark_tasks/tool_reliability_tasks.rs"]
mod tool_reliability_tasks;

/// Benchmark suite tiers — controls which tasks are included.
///
/// Suites are cumulative: `Speed` includes only speed-tier tasks,
/// `Standard` includes speed + standard, `Frontier`/`All` include everything,
/// and `PromptRegression` isolates prompt-regression tasks only.
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum BenchmarkSuite {
    Speed,
    Standard,
    Frontier,
    PromptRegression,
    All,
}

impl BenchmarkSuite {
    /// Parse from a string (case-insensitive).
    pub fn parse_str(s: &str) -> Option<Self> {
        match s.to_lowercase().as_str() {
            "speed" => Some(Self::Speed),
            "standard" => Some(Self::Standard),
            "frontier" => Some(Self::Frontier),
            "prompt_regression" | "prompt-regression" => Some(Self::PromptRegression),
            "all" => Some(Self::All),
            _ => None,
        }
    }
}

impl std::fmt::Display for BenchmarkSuite {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Speed => write!(f, "speed"),
            Self::Standard => write!(f, "standard"),
            Self::Frontier => write!(f, "frontier"),
            Self::PromptRegression => write!(f, "prompt_regression"),
            Self::All => write!(f, "all"),
        }
    }
}

/// Programming language for a benchmark task.
#[derive(Debug, Clone, Copy, PartialEq, serde::Serialize)]
#[serde(rename_all = "snake_case")]
pub enum Language {
    Rust,
    Python,
    JavaScript,
    Go,
}

impl Language {
    /// Parse from a string (case-insensitive).
    pub fn parse_str(s: &str) -> Option<Self> {
        match s.to_lowercase().as_str() {
            "rust" | "rs" => Some(Self::Rust),
            "python" | "py" => Some(Self::Python),
            "javascript" | "js" | "typescript" | "ts" => Some(Self::JavaScript),
            "go" | "golang" => Some(Self::Go),
            _ => None,
        }
    }
}

impl std::fmt::Display for Language {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Rust => write!(f, "rust"),
            Self::Python => write!(f, "python"),
            Self::JavaScript => write!(f, "javascript"),
            Self::Go => write!(f, "go"),
        }
    }
}

/// Difficulty category for a benchmark task.
#[derive(Debug, Clone, Copy, serde::Serialize)]
#[serde(rename_all = "snake_case")]
pub enum TaskCategory {
    Simple,
    Medium,
    Hard,
    ToolUse,
    RealWorld,
    Agentic,
    /// Tasks requiring multiple tool calls in sequence.
    MultiStep,
    /// Tasks where the first attempt should fail, testing recovery.
    SelfCorrection,
    /// Tasks that must follow specific constraints.
    ConstraintFollowing,
    /// Multi-language tasks (Python, TypeScript/JS, Go).
    MultiLang,
    /// Security vulnerability detection and fixing.
    Security,
    /// Generating tests for given code.
    TestGeneration,
    /// Scripted headless tasks focused on tool execution reliability.
    ToolReliability,
    /// Representative coding tasks for general implementation quality.
    NormalCoding,
    /// Compact but realistic coding tasks from scratch.
    SmallCoding,
    /// Longer coding tasks with higher tool/recovery pressure.
    StressCoding,
    /// Project-scale tasks that span multiple files.
    LargeProject,
    /// Tasks centered on creating/fixing tests alongside code.
    TestHeavy,
    /// Refactor/migration/cleanup tasks with regression sensitivity.
    Maintenance,
    /// Tool-use tasks that intentionally require recovery from failures.
    ToolRecovery,
    /// Product-surface smoke journeys (TUI/Desktop/Web/session/permissions).
    ProductSmoke,
    /// MCP integration and multi-server interoperability tasks.
    McpIntegration,
    /// LSP-adjacent presence and language project smoke tasks.
    LspSmoke,
    /// Prompt-sensitive regression checks for agent behavior discipline.
    PromptRegression,
}

impl std::fmt::Display for TaskCategory {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Simple => write!(f, "simple"),
            Self::Medium => write!(f, "medium"),
            Self::Hard => write!(f, "hard"),
            Self::ToolUse => write!(f, "tool_use"),
            Self::RealWorld => write!(f, "real_world"),
            Self::Agentic => write!(f, "agentic"),
            Self::MultiStep => write!(f, "multi_step"),
            Self::SelfCorrection => write!(f, "self_correction"),
            Self::ConstraintFollowing => write!(f, "constraint_following"),
            Self::MultiLang => write!(f, "multi_lang"),
            Self::Security => write!(f, "security"),
            Self::TestGeneration => write!(f, "test_generation"),
            Self::ToolReliability => write!(f, "tool_reliability"),
            Self::NormalCoding => write!(f, "normal_coding"),
            Self::SmallCoding => write!(f, "small_coding"),
            Self::StressCoding => write!(f, "stress_coding"),
            Self::LargeProject => write!(f, "large_project"),
            Self::TestHeavy => write!(f, "test_heavy"),
            Self::Maintenance => write!(f, "maintenance"),
            Self::ToolRecovery => write!(f, "tool_recovery"),
            Self::ProductSmoke => write!(f, "product_smoke"),
            Self::McpIntegration => write!(f, "mcp_integration"),
            Self::LspSmoke => write!(f, "lsp_smoke"),
            Self::PromptRegression => write!(f, "prompt_regression"),
        }
    }
}

impl TaskCategory {
    /// Which benchmark suite this category belongs to (minimum suite that includes it).
    pub fn min_suite(&self) -> BenchmarkSuite {
        match self {
            Self::Simple | Self::Medium | Self::Hard | Self::ToolUse | Self::RealWorld => {
                BenchmarkSuite::Speed
            }
            Self::Agentic
            | Self::ConstraintFollowing
            | Self::SelfCorrection
            | Self::MultiLang
            | Self::Security
            | Self::ToolReliability
            | Self::NormalCoding
            | Self::SmallCoding
            | Self::PromptRegression => BenchmarkSuite::Standard,
            Self::MultiStep
            | Self::StressCoding
            | Self::LargeProject
            | Self::TestHeavy
            | Self::Maintenance
            | Self::ToolRecovery
            | Self::ProductSmoke
            | Self::McpIntegration
            | Self::LspSmoke => BenchmarkSuite::Frontier,
            Self::TestGeneration => BenchmarkSuite::Speed,
        }
    }
}

/// Filter tasks to those included in the given suite.
///
/// Speed includes only speed-tier tasks.
/// Standard includes speed + standard tasks.
/// Frontier and All include everything.
/// PromptRegression includes only prompt-regression tasks.
pub fn filter_tasks_by_suite(
    tasks: Vec<BenchmarkTask>,
    suite: BenchmarkSuite,
) -> Vec<BenchmarkTask> {
    match suite {
        BenchmarkSuite::All | BenchmarkSuite::Frontier => tasks,
        BenchmarkSuite::Standard => tasks
            .into_iter()
            .filter(|t| {
                matches!(
                    t.category.min_suite(),
                    BenchmarkSuite::Speed | BenchmarkSuite::Standard
                )
            })
            .collect(),
        BenchmarkSuite::Speed => tasks
            .into_iter()
            .filter(|t| matches!(t.category.min_suite(), BenchmarkSuite::Speed))
            .collect(),
        BenchmarkSuite::PromptRegression => tasks
            .into_iter()
            .filter(|t| matches!(t.category, TaskCategory::PromptRegression))
            .collect(),
    }
}

pub fn filter_tasks_by_name(tasks: Vec<BenchmarkTask>, filter: Option<&str>) -> Vec<BenchmarkTask> {
    let Some(filter) = filter.map(str::trim).filter(|value| !value.is_empty()) else {
        return tasks;
    };

    let needles: Vec<String> = filter
        .split(',')
        .map(str::trim)
        .filter(|value| !value.is_empty())
        .map(|value| value.to_lowercase())
        .collect();

    if needles.is_empty() {
        return tasks;
    }

    tasks
        .into_iter()
        .filter(|task| {
            let task_name = task.name.to_lowercase();
            needles
                .iter()
                .any(|needle| task_name == *needle || task_name.contains(needle))
        })
        .collect()
}

#[cfg(test)]
mod task_filter_tests {
    use super::*;

    fn task(name: &'static str) -> BenchmarkTask {
        task_with_category(name, TaskCategory::Simple)
    }

    fn task_with_category(name: &'static str, category: TaskCategory) -> BenchmarkTask {
        BenchmarkTask {
            name,
            prompt: String::new(),
            expected_patterns: vec![],
            category,
            needs_tools: false,
            test_harness: None,
            expected_min_tools: None,
        }
    }

    #[test]
    fn filter_tasks_by_name_matches_exact_and_substring_queries() {
        let tasks = vec![
            task("rule_guided_typescript"),
            task("delegated_config_bugfix"),
            task("cross_file_refactor"),
        ];

        let filtered = filter_tasks_by_name(tasks, Some("delegated,typescript"));
        let names: Vec<_> = filtered.into_iter().map(|task| task.name).collect();

        assert_eq!(
            names,
            vec!["rule_guided_typescript", "delegated_config_bugfix"]
        );
    }

    #[test]
    fn filter_tasks_by_name_returns_all_tasks_when_filter_is_empty() {
        let tasks = vec![task("one"), task("two")];
        let filtered = filter_tasks_by_name(tasks.clone(), Some("  "));

        assert_eq!(filtered.len(), tasks.len());
    }

    #[test]
    fn speed_suite_excludes_new_standard_and_frontier_categories() {
        let tasks = vec![
            task_with_category("speed_baseline", TaskCategory::Simple),
            task_with_category("small_coding_stub", TaskCategory::SmallCoding),
            task_with_category("stress_coding_stub", TaskCategory::StressCoding),
        ];

        let filtered = filter_tasks_by_suite(tasks, BenchmarkSuite::Speed);
        let names: Vec<_> = filtered.into_iter().map(|task| task.name).collect();

        assert_eq!(names, vec!["speed_baseline"]);
    }

    #[test]
    fn standard_suite_includes_small_coding_but_excludes_frontier_scaffolding() {
        let tasks = vec![
            task_with_category("speed_baseline", TaskCategory::Simple),
            task_with_category("small_coding_stub", TaskCategory::SmallCoding),
            task_with_category("large_project_stub", TaskCategory::LargeProject),
            task_with_category("mcp_integration_stub", TaskCategory::McpIntegration),
        ];

        let filtered = filter_tasks_by_suite(tasks, BenchmarkSuite::Standard);
        let names: Vec<_> = filtered.into_iter().map(|task| task.name).collect();

        assert_eq!(names, vec!["speed_baseline", "small_coding_stub"]);
    }

    #[test]
    fn prompt_regression_suite_includes_only_prompt_regression_tasks() {
        let tasks = vec![
            task_with_category("speed_baseline", TaskCategory::Simple),
            task_with_category("prompt_lane", TaskCategory::PromptRegression),
            task_with_category("frontier_lane", TaskCategory::StressCoding),
        ];

        let filtered = filter_tasks_by_suite(tasks, BenchmarkSuite::PromptRegression);
        let names: Vec<_> = filtered.into_iter().map(|task| task.name).collect();

        assert_eq!(names, vec!["prompt_lane"]);
    }
}

/// Test harness for compile-and-test validation.
#[derive(Debug, Clone)]
pub struct TestHarness {
    /// Test code to append to the extracted model output (Tier 2)
    /// or to the file after agent edits (Tier 3).
    pub test_code: &'static str,
    /// For Tier 3: initial buggy file content to write before running.
    pub setup_code: Option<&'static str>,
    /// Number of test functions in the harness.
    pub test_count: usize,
    /// Programming language for this harness. Defaults to Rust.
    pub language: Language,
}

/// A single benchmark task.
#[derive(Debug, Clone)]
pub struct BenchmarkTask {
    /// Short identifier for the task (used in table headers).
    pub name: &'static str,
    /// The prompt sent to the model. Use `String` for runtime path interpolation.
    pub prompt: String,
    /// Regex patterns that should appear in a successful response.
    pub expected_patterns: Vec<&'static str>,
    /// Difficulty category.
    pub category: TaskCategory,
    /// Whether this task requires tool use (agent mode).
    pub needs_tools: bool,
    /// Optional test harness for compile-and-test validation.
    pub test_harness: Option<TestHarness>,
    /// Minimum expected number of tool calls for efficiency scoring.
    /// Only meaningful for tool-using tasks. Used to compute `tool_efficiency_score`.
    pub expected_min_tools: Option<u32>,
}

impl BenchmarkTask {
    /// Returns the programming language for this task (from test harness, defaults to Rust).
    pub fn language(&self) -> Language {
        self.test_harness
            .as_ref()
            .map_or(Language::Rust, |h| h.language)
    }
}

/// Setup code for cross-file refactor: lib.rs
const MULTI_FILE_REFACTOR_LIB: &str = r#"mod utils;

fn compute(x: i32, y: i32) -> i32 {
    let sum = x + y;
    let product = x * y;
    sum + product
}

pub fn process(a: i32, b: i32) -> i32 {
    compute(a, b) * 2
}

pub fn format_output(val: i32) -> String {
    format!("Result: {}", val)
}
"#;

/// Test harness for cross-file refactor — validates against combined lib.rs + utils.rs.
const MULTI_FILE_REFACTOR_TESTS: &str = r#"
#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_process_still_works() {
        assert_eq!(process(3, 4), 38); // (3+4+3*4)*2 = (7+12)*2 = 38
    }

    #[test]
    fn test_format_output() {
        assert_eq!(format_output(38), "Result: 38");
    }
}
"#;

/// Setup code for find-and-fix across files: client.rs (buggy)
const MULTI_FILE_CLIENT: &str = r#"// BUG: references Config::default() but uses wrong field names
pub struct Client {
    retries: u32,
    timeout: u64,
}

impl Client {
    pub fn new(config: &super::config::Config) -> Self {
        Self {
            retries: config.max_retry,      // BUG: field is max_retries
            timeout: config.timeout,         // BUG: field is timeout_ms
        }
    }

    pub fn max_retries(&self) -> u32 {
        self.retries
    }

    pub fn timeout_ms(&self) -> u64 {
        self.timeout
    }
}
"#;

/// Test harness for find-and-fix across files — validates the combined output.
const MULTI_FILE_CLIENT_TESTS: &str = r#"
#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_client_max_retries() {
        let config = Config::default();
        let client = Client::new(&config);
        assert_eq!(client.max_retries(), 3);
    }

    #[test]
    fn test_client_timeout_ms() {
        let config = Config::default();
        let client = Client::new(&config);
        assert_eq!(client.timeout_ms(), 5000);
    }
}
"#;

/// Returns Tier 3 multi-file agentic tasks. These require a temp directory path.
pub fn multi_file_tasks(temp_dir: &std::path::Path) -> Vec<BenchmarkTask> {
    let refactor_dir = temp_dir.join("cross_file_refactor");
    let fix_dir = temp_dir.join("find_fix_across_files");

    vec![
        BenchmarkTask {
            name: "cross_file_refactor",
            prompt: format!(
                "There are three files in {dir}: `main.rs`, `lib.rs`, and `utils.rs`. The \
                 `compute()` function in `lib.rs` should be extracted to `utils.rs` as a public \
                 function. Update `lib.rs` to import and call `utils::compute()` instead of \
                 having its own copy. Make sure everything still works.",
                dir = refactor_dir.display()
            ),
            expected_patterns: vec![r"pub fn compute", r"utils::compute"],
            category: TaskCategory::MultiStep,
            needs_tools: true,
            test_harness: Some(TestHarness {
                test_code: MULTI_FILE_REFACTOR_TESTS,
                setup_code: Some(MULTI_FILE_REFACTOR_LIB),
                test_count: 2,
                language: Language::Rust,
            }),
            expected_min_tools: Some(5),
        },
        BenchmarkTask {
            name: "find_and_fix_across_files",
            prompt: format!(
                "There are two files in {dir}: `config.rs` and `client.rs`. The `Client::new()` \
                 function references wrong field names from `Config`. Find and fix the bugs so \
                 the code compiles. Don't change `Config`'s field names — fix the `Client` code.",
                dir = fix_dir.display()
            ),
            expected_patterns: vec![r"max_retries", r"timeout_ms"],
            category: TaskCategory::MultiStep,
            needs_tools: true,
            test_harness: Some(TestHarness {
                test_code: MULTI_FILE_CLIENT_TESTS,
                setup_code: Some(MULTI_FILE_CLIENT),
                test_count: 2,
                language: Language::Rust,
            }),
            expected_min_tools: Some(4),
        },
    ]
}

#[allow(unused_imports)]
pub use agent_quality_tasks::agent_quality_tasks;
#[allow(unused_imports)]
pub use agentic_tasks::agentic_tasks;
#[allow(unused_imports)]
pub use large_project_tasks::large_project_tasks;
#[allow(unused_imports)]
pub use lsp_smoke_tasks::lsp_smoke_tasks;
#[allow(unused_imports)]
pub use maintenance_tasks::maintenance_tasks;
#[allow(unused_imports)]
pub use mcp_integration_tasks::mcp_integration_tasks;
#[allow(unused_imports)]
pub use multi_language_tasks::{go_tasks, python_tasks, typescript_tasks};
#[allow(unused_imports)]
pub use normal_coding_tasks::normal_coding_tasks;
#[allow(unused_imports)]
pub use product_smoke_tasks::product_smoke_tasks;
#[allow(unused_imports)]
pub use prompt_regression_tasks::prompt_regression_tasks;
#[allow(unused_imports)]
pub use security_and_test_tasks::{security_tasks, test_generation_tasks};
#[allow(unused_imports)]
pub use small_coding_tasks::small_coding_tasks;
#[allow(unused_imports)]
pub use speed_tasks::{advanced_rust_tasks, default_tasks};
#[allow(unused_imports)]
pub use stress_coding_tasks::stress_coding_tasks;
#[allow(unused_imports)]
pub use test_heavy_tasks::test_heavy_tasks;
#[allow(unused_imports)]
pub use tool_recovery_tasks::tool_recovery_tasks;
#[allow(unused_imports)]
pub use tool_reliability_tasks::tool_reliability_tasks;
