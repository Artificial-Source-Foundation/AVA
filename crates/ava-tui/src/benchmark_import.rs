//! Import external benchmark tasks from Aider Polyglot (Exercism) format.
//!
//! Reads exercise directories from a locally cloned polyglot-benchmark repo
//! and converts them to AVA's `BenchmarkTask` format.
//!
//! ## Usage
//!
//! ```bash
//! # Clone the benchmark repo first:
//! git clone https://github.com/Aider-AI/polyglot-benchmark ~/.ava/benchmarks/polyglot
//!
//! # Then run with --import-polyglot:
//! cargo run --bin ava -- --benchmark --import-polyglot ~/.ava/benchmarks/polyglot \
//!   --models "inception:mercury-2,openrouter:anthropic/claude-haiku-4.5"
//! ```
//!
//! ## Supported Languages
//!
//! Currently imports exercises for languages AVA can validate:
//! - **Rust** — compiled with `rustc --edition 2021 --test`
//! - **Python** — run with `python3`
//! - **JavaScript** — run with `node`
//! - **Go** — run with `go run`
//!
//! C++ and Java exercises are skipped (no validator in AVA yet).

use std::path::{Path, PathBuf};

use color_eyre::eyre::{eyre, Result};

use crate::benchmark_tasks::{BenchmarkTask, Language, TaskCategory, TestHarness};

/// Supported language directories in the polyglot-benchmark repo.
const SUPPORTED_LANGS: &[(&str, Language)] = &[
    ("rust", Language::Rust),
    ("python", Language::Python),
    ("javascript", Language::JavaScript),
    ("go", Language::Go),
];

/// Import exercises from a polyglot-benchmark repo directory.
///
/// Returns a list of `BenchmarkTask`s ready to run through AVA's benchmark runner.
/// Exercises that can't be parsed (missing files, unsupported format) are skipped
/// with a warning printed to stderr.
pub fn import_polyglot(repo_path: &Path) -> Result<Vec<BenchmarkTask>> {
    if !repo_path.exists() {
        return Err(eyre!(
            "Polyglot benchmark repo not found at {}. Clone it first:\n  \
             git clone https://github.com/Aider-AI/polyglot-benchmark {}",
            repo_path.display(),
            repo_path.display()
        ));
    }

    let mut tasks = Vec::new();
    let mut skipped = 0;

    for &(lang_dir, language) in SUPPORTED_LANGS {
        let exercises_dir = repo_path.join(lang_dir).join("exercises").join("practice");
        if !exercises_dir.exists() {
            eprintln!(
                "[import] Skipping {}: no exercises/practice directory",
                lang_dir
            );
            continue;
        }

        let mut entries: Vec<_> = std::fs::read_dir(&exercises_dir)
            .map_err(|e| eyre!("Failed to read {}: {}", exercises_dir.display(), e))?
            .filter_map(|e| e.ok())
            .filter(|e| e.file_type().map(|ft| ft.is_dir()).unwrap_or(false))
            .collect();
        entries.sort_by_key(|e| e.file_name());

        for entry in entries {
            let exercise_dir = entry.path();
            match import_exercise(&exercise_dir, language) {
                Ok(task) => tasks.push(task),
                Err(e) => {
                    skipped += 1;
                    eprintln!(
                        "[import] Skipping {}/{}: {}",
                        lang_dir,
                        entry.file_name().to_string_lossy(),
                        e
                    );
                }
            }
        }
    }

    eprintln!(
        "[import] Imported {} tasks from polyglot-benchmark ({} skipped)",
        tasks.len(),
        skipped
    );

    Ok(tasks)
}

/// Import a single Exercism exercise directory.
fn import_exercise(exercise_dir: &Path, language: Language) -> Result<BenchmarkTask> {
    let slug = exercise_dir
        .file_name()
        .ok_or_else(|| eyre!("No directory name"))?
        .to_string_lossy()
        .to_string();

    // Read instructions
    let instructions = read_instructions(exercise_dir)?;

    // Read test file
    let (test_code, test_count) = read_test_file(exercise_dir, language)?;

    // Build the prompt
    let prompt = build_prompt(&slug, &instructions, language);

    // Leak strings to get 'static lifetimes (fine for benchmark process lifetime)
    let name: &'static str = Box::leak(format!("poly_{}", slug.replace('-', "_")).into_boxed_str());
    let test_code_static: &'static str = Box::leak(test_code.into_boxed_str());

    Ok(BenchmarkTask {
        name,
        prompt,
        expected_patterns: vec![], // Exercism tasks use compile+test, not regex
        category: TaskCategory::MultiLang,
        needs_tools: false,
        test_harness: Some(TestHarness {
            test_code: test_code_static,
            setup_code: None,
            test_count,
            language,
        }),
        expected_min_tools: None,
    })
}

/// Read instructions from .docs/instructions.md (and optionally introduction.md).
fn read_instructions(exercise_dir: &Path) -> Result<String> {
    let docs_dir = exercise_dir.join(".docs");
    let instructions_path = docs_dir.join("instructions.md");

    if !instructions_path.exists() {
        return Err(eyre!("No .docs/instructions.md"));
    }

    let mut text = String::new();

    // Optional introduction
    let intro_path = docs_dir.join("introduction.md");
    if intro_path.exists() {
        if let Ok(intro) = std::fs::read_to_string(&intro_path) {
            text.push_str(&intro);
            text.push_str("\n\n");
        }
    }

    let instructions =
        std::fs::read_to_string(&instructions_path).map_err(|e| eyre!("Read error: {}", e))?;
    text.push_str(&instructions);

    Ok(text)
}

/// Read the test file and count test functions.
fn read_test_file(exercise_dir: &Path, language: Language) -> Result<(String, usize)> {
    let test_path = find_test_file(exercise_dir, language)?;
    let content =
        std::fs::read_to_string(&test_path).map_err(|e| eyre!("Failed to read test file: {}", e))?;

    let test_count = count_tests(&content, language);
    if test_count == 0 {
        return Err(eyre!("No test functions found in {}", test_path.display()));
    }

    // For Rust, strip #[ignore] attributes so all tests run
    let content = if language == Language::Rust {
        strip_rust_ignores(&content)
    } else {
        content
    };

    Ok((content, test_count))
}

/// Find the test file for an exercise based on language conventions.
fn find_test_file(exercise_dir: &Path, language: Language) -> Result<PathBuf> {
    let slug = exercise_dir
        .file_name()
        .unwrap()
        .to_string_lossy()
        .replace('-', "_");

    let candidates: Vec<PathBuf> = match language {
        Language::Rust => vec![
            exercise_dir.join("tests").join(format!("{}.rs", slug)),
            exercise_dir.join("tests").join("lib.rs"),
        ],
        Language::Python => vec![
            exercise_dir.join(format!("{}_test.py", slug)),
            exercise_dir.join("test.py"),
        ],
        Language::JavaScript => vec![
            exercise_dir.join(format!("{}.spec.js", slug)),
            exercise_dir.join(format!("{}.test.js", slug)),
        ],
        Language::Go => vec![
            exercise_dir.join(format!("{}_test.go", slug)),
            exercise_dir.join("cases_test.go"),
        ],
    };

    for candidate in &candidates {
        if candidate.exists() {
            return Ok(candidate.clone());
        }
    }

    Err(eyre!(
        "No test file found. Tried: {}",
        candidates
            .iter()
            .map(|p| p.file_name().unwrap().to_string_lossy().to_string())
            .collect::<Vec<_>>()
            .join(", ")
    ))
}

/// Count test functions in test source code.
fn count_tests(content: &str, language: Language) -> usize {
    match language {
        Language::Rust => content.matches("#[test]").count(),
        Language::Python => {
            content
                .lines()
                .filter(|l| {
                    let trimmed = l.trim();
                    trimmed.starts_with("def test_") || trimmed.starts_with("async def test_")
                })
                .count()
        }
        Language::JavaScript => {
            content
                .lines()
                .filter(|l| {
                    let trimmed = l.trim();
                    trimmed.starts_with("it(") || trimmed.starts_with("test(")
                })
                .count()
        }
        Language::Go => {
            content
                .lines()
                .filter(|l| l.trim().starts_with("func Test"))
                .count()
        }
    }
}

/// Strip `#[ignore]` attributes from Rust test files so all tests run.
/// Exercism uses `#[ignore]` on most tests by default — students enable them progressively.
fn strip_rust_ignores(content: &str) -> String {
    content
        .lines()
        .filter(|line| {
            let trimmed = line.trim();
            trimmed != "#[ignore]"
        })
        .collect::<Vec<_>>()
        .join("\n")
}

/// Build the prompt for an imported exercise.
fn build_prompt(slug: &str, instructions: &str, language: Language) -> String {
    let lang_name = match language {
        Language::Rust => "Rust",
        Language::Python => "Python",
        Language::JavaScript => "JavaScript",
        Language::Go => "Go",
    };

    let func_name = slug.replace('-', "_");

    format!(
        "Solve the following exercise in {lang_name}. Output ONLY the solution code, \
         no explanations.\n\n\
         ## Exercise: {slug}\n\n\
         {instructions}\n\n\
         Write a complete {lang_name} solution. The function/module should be named `{func_name}` \
         (or follow the language's naming conventions). Your code will be tested against \
         a unit test suite."
    )
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_count_rust_tests() {
        let content = r#"
#[test]
fn test_one() {}
#[test]
fn test_two() {}
"#;
        assert_eq!(count_tests(content, Language::Rust), 2);
    }

    #[test]
    fn test_count_python_tests() {
        let content = r#"
def test_hello():
    pass
def test_goodbye():
    pass
def not_a_test():
    pass
"#;
        assert_eq!(count_tests(content, Language::Python), 2);
    }

    #[test]
    fn test_count_go_tests() {
        let content = r#"
func TestHello(t *testing.T) {}
func TestWorld(t *testing.T) {}
func helper() {}
"#;
        assert_eq!(count_tests(content, Language::Go), 2);
    }

    #[test]
    fn test_strip_rust_ignores() {
        let input = "#[test]\n#[ignore]\nfn test_one() {}\n#[test]\nfn test_two() {}";
        let result = strip_rust_ignores(input);
        assert!(!result.contains("#[ignore]"));
        assert_eq!(result.matches("#[test]").count(), 2);
    }

    #[test]
    fn test_count_js_tests() {
        let content = r#"
it('should work', () => {});
test('another', () => {});
describe('suite', () => {});
"#;
        assert_eq!(count_tests(content, Language::JavaScript), 2);
    }
}
