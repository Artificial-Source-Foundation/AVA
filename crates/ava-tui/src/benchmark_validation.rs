use std::sync::LazyLock;

use regex::Regex;

use crate::benchmark_support::compile_and_test;
use crate::benchmark_tasks::{Language, TestHarness};

/// Extract code from model output, aware of the target language.
///
/// Looks for fenced code blocks tagged with the appropriate language, then falls
/// back to generic fenced blocks, then to language-specific heuristics.
fn extract_code(output: &str, language: Language) -> Option<String> {
    // Language-specific fenced block tags to try first
    let lang_tags: &[&str] = match language {
        Language::Rust => &["rust"],
        Language::Python => &["python", "py"],
        Language::JavaScript => &["javascript", "js", "jsx", "typescript", "ts", "tsx"],
        Language::Go => &["go", "golang"],
    };

    // Try language-specific fenced blocks first
    for tag in lang_tags {
        let pattern = format!(r"(?s)```{}\s*\n(.*?)```", tag);
        if let Ok(re) = Regex::new(&pattern) {
            if let Some(cap) = re.captures(output) {
                return Some(cap[1].to_string());
            }
        }
    }

    // Try generic ``` blocks
    let re_generic = Regex::new(r"(?s)```\s*\n(.*?)```").ok()?;
    if let Some(cap) = re_generic.captures(output) {
        return Some(cap[1].to_string());
    }

    // Language-specific heuristics for unfenced code
    match language {
        Language::Rust => {
            // Try to find code that looks like a function definition without fences
            let re_fn = Regex::new(r"(?s)((?:use\s+.*?;\s*)*(?:pub\s+)?fn\s+\w+.*?\n\})").ok()?;
            if let Some(cap) = re_fn.captures(output) {
                return Some(cap[1].to_string());
            }
            if output.contains("fn ") {
                return Some(output.to_string());
            }
        }
        Language::Python => {
            // Look for def or class definitions
            let re_def =
                Regex::new(r"(?s)((?:import\s+.*\n|from\s+.*\n)*(?:def|class)\s+\w+.*)").ok()?;
            if let Some(cap) = re_def.captures(output) {
                return Some(cap[1].to_string());
            }
            if output.contains("def ") || output.contains("class ") {
                return Some(output.to_string());
            }
        }
        Language::JavaScript => {
            // Look for function definitions or const/let assignments
            let re_fn = Regex::new(r"(?s)((?:const|let|var|function)\s+\w+.*)").ok()?;
            if let Some(cap) = re_fn.captures(output) {
                return Some(cap[1].to_string());
            }
            if output.contains("function ") || output.contains("const ") {
                return Some(output.to_string());
            }
        }
        Language::Go => {
            // Look for func or type definitions
            let re_fn =
                Regex::new(r"(?s)((?:package\s+\w+\s*\n)?(?:import\s+.*\n)*(?:type|func)\s+\w+.*)")
                    .ok()?;
            if let Some(cap) = re_fn.captures(output) {
                return Some(cap[1].to_string());
            }
            if output.contains("func ") || output.contains("type ") {
                return Some(output.to_string());
            }
        }
    }

    None
}

/// Tier 2: Extract code from model output, write to temp file with tests, compile and run.
///
/// Dispatches to language-specific validation based on `harness.language`.
pub(crate) async fn run_tier2_validation(
    model_output: &str,
    harness: &TestHarness,
) -> (Option<bool>, Option<usize>, Option<usize>, Option<String>) {
    match harness.language {
        Language::Rust => run_tier2_rust(model_output, harness).await,
        Language::Python => run_tier2_python(model_output, harness).await,
        Language::JavaScript => run_tier2_javascript(model_output, harness).await,
        Language::Go => run_tier2_go(model_output, harness).await,
    }
}

/// Tier 2 validation for Rust: compile with rustc --test, run the binary.
async fn run_tier2_rust(
    model_output: &str,
    harness: &TestHarness,
) -> (Option<bool>, Option<usize>, Option<usize>, Option<String>) {
    let Some(code) = extract_code(model_output, Language::Rust) else {
        return (
            Some(false),
            None,
            None,
            Some("Could not extract Rust code from output".to_string()),
        );
    };

    // Build the full source: extracted code + use statements for HashMap if needed + test harness
    let mut full_source = String::new();

    // Add common imports if not present
    if harness.test_code.contains("HashMap") && !code.contains("use std::collections::HashMap") {
        full_source.push_str("use std::collections::HashMap;\n");
    }
    if harness.test_code.contains("Hash") && !code.contains("use std::hash::Hash") {
        full_source.push_str("use std::hash::Hash;\n");
    }

    full_source.push_str(&code);
    full_source.push_str("\n\nfn main() {}\n");
    full_source.push_str(harness.test_code);

    compile_and_test(&full_source, harness.test_count).await
}

/// Tier 2 validation for Python: concatenate extracted code + test harness, run with python3.
async fn run_tier2_python(
    model_output: &str,
    harness: &TestHarness,
) -> (Option<bool>, Option<usize>, Option<usize>, Option<String>) {
    let Some(code) = extract_code(model_output, Language::Python) else {
        return (
            Some(false),
            None,
            None,
            Some("Could not extract Python code from output".to_string()),
        );
    };

    let full_source = format!("{}\n{}", code, harness.test_code);
    run_script("python3", &full_source, "py", harness.test_count).await
}

/// Tier 2 validation for JavaScript: concatenate extracted code + test harness, run with node.
async fn run_tier2_javascript(
    model_output: &str,
    harness: &TestHarness,
) -> (Option<bool>, Option<usize>, Option<usize>, Option<String>) {
    let Some(code) = extract_code(model_output, Language::JavaScript) else {
        return (
            Some(false),
            None,
            None,
            Some("Could not extract JavaScript code from output".to_string()),
        );
    };

    let full_source = format!("{}\n{}", code, harness.test_code);
    run_script("node", &full_source, "js", harness.test_count).await
}

/// Compiled regex for stripping `func main()` bodies from model-generated Go code.
///
/// Using `LazyLock` ensures the pattern is validated at first use (in tests)
/// rather than panicking at runtime with `.unwrap()`.
static RE_GO_MAIN: LazyLock<Regex> = LazyLock::new(|| {
    Regex::new(r"(?s)func\s+main\s*\(\s*\)\s*\{[^}]*\}")
        .expect("RE_GO_MAIN: static regex pattern is invalid — this is a compile-time bug")
});

/// Tier 2 validation for Go: wrap extracted code in package main with imports, run with `go run`.
async fn run_tier2_go(
    model_output: &str,
    harness: &TestHarness,
) -> (Option<bool>, Option<usize>, Option<usize>, Option<String>) {
    let Some(code) = extract_code(model_output, Language::Go) else {
        return (
            Some(false),
            None,
            None,
            Some("Could not extract Go code from output".to_string()),
        );
    };

    // Build a single-file Go program. If the model already included `package main`,
    // we skip adding it again; otherwise we wrap everything.
    let mut full_source = String::new();

    let has_package = code.contains("package ");

    if !has_package {
        full_source.push_str("package main\n\n");
    }

    // Gather required imports from both code and test harness
    let combined = format!("{}\n{}", code, harness.test_code);
    let mut imports: Vec<&str> = Vec::new();
    if combined.contains("fmt.") && !combined.contains("\"fmt\"") {
        imports.push("\"fmt\"");
    }
    if combined.contains("os.") && !combined.contains("\"os\"") {
        imports.push("\"os\"");
    }
    if combined.contains("sync.") && !combined.contains("\"sync\"") {
        imports.push("\"sync\"");
    }

    if !imports.is_empty() && !code.contains("import ") {
        full_source.push_str("import (\n");
        for imp in &imports {
            full_source.push_str(&format!("    {}\n", imp));
        }
        full_source.push_str(")\n\n");
    }

    // Strip package/import lines from model code if we already added them
    let code_lines: Vec<&str> = code.lines().collect();
    let mut skip_import_block = false;
    for line in &code_lines {
        let trimmed = line.trim();
        if !has_package && trimmed.starts_with("package ") {
            continue;
        }
        if !has_package && trimmed == "import (" {
            skip_import_block = true;
            continue;
        }
        if skip_import_block {
            if trimmed == ")" {
                skip_import_block = false;
            }
            continue;
        }
        if !has_package && trimmed.starts_with("import ") && !trimmed.contains("(") {
            continue;
        }
        full_source.push_str(line);
        full_source.push('\n');
    }

    // Remove any `func main()` from the model code since the test harness provides one
    let source_without_main = RE_GO_MAIN.replace_all(&full_source, "").to_string();

    let final_source = format!("{}\n{}", source_without_main, harness.test_code);
    run_script("go", &final_source, "go", harness.test_count).await
}

/// Run a script file with the given interpreter and check results.
///
/// For Go, uses `go run <file>`. For others, uses `<interpreter> <file>`.
/// Returns the standard (compile_success, tests_passed, tests_total, error) tuple.
async fn run_script(
    interpreter: &str,
    source: &str,
    extension: &str,
    expected_test_count: usize,
) -> (Option<bool>, Option<usize>, Option<usize>, Option<String>) {
    let temp_dir = match tempfile::tempdir() {
        Ok(d) => d,
        Err(e) => {
            return (
                Some(false),
                None,
                None,
                Some(format!("Failed to create temp dir: {}", e)),
            );
        }
    };

    let filename = format!("bench_test.{}", extension);
    let source_path = temp_dir.path().join(&filename);

    if let Err(e) = tokio::fs::write(&source_path, source).await {
        return (
            Some(false),
            None,
            None,
            Some(format!("Failed to write source: {}", e)),
        );
    }

    // Build the command based on interpreter
    let output = if interpreter == "go" {
        tokio::process::Command::new("go")
            .arg("run")
            .arg(source_path.to_str().unwrap_or(&filename))
            .current_dir(temp_dir.path())
            .output()
            .await
    } else {
        tokio::process::Command::new(interpreter)
            .arg(source_path.to_str().unwrap_or(&filename))
            .current_dir(temp_dir.path())
            .output()
            .await
    };

    let result = match output {
        Ok(r) => r,
        Err(e) => {
            return (
                Some(false),
                None,
                None,
                Some(format!("Failed to run {} {}: {}", interpreter, filename, e)),
            );
        }
    };

    let stdout = String::from_utf8_lossy(&result.stdout);
    let stderr = String::from_utf8_lossy(&result.stderr);

    if result.status.success() {
        // For Python unittest, parse "Ran N tests" from stderr
        let tests_passed = if interpreter == "python3" {
            parse_python_test_count(&stderr).unwrap_or(expected_test_count)
        } else {
            expected_test_count
        };
        (Some(true), Some(tests_passed), Some(tests_passed), None)
    } else {
        let error_output = if !stderr.is_empty() {
            stderr.to_string()
        } else {
            stdout.to_string()
        };
        let error_msg = if error_output.len() > 500 {
            format!("{}...", &error_output[..500])
        } else {
            error_output
        };

        // For Python, try to parse how many tests passed vs failed
        if interpreter == "python3" {
            let (passed, failed) = parse_python_test_results(&stderr);
            if passed + failed > 0 {
                return (
                    Some(true),
                    Some(passed),
                    Some(passed + failed),
                    Some(error_msg),
                );
            }
        }

        (
            Some(false),
            Some(0),
            Some(expected_test_count),
            Some(error_msg),
        )
    }
}

/// Parse "Ran N tests" from Python unittest output (printed to stderr).
fn parse_python_test_count(output: &str) -> Option<usize> {
    let re = Regex::new(r"Ran (\d+) test").ok()?;
    re.captures(output)
        .and_then(|cap| cap[1].parse::<usize>().ok())
}

/// Parse Python unittest failure details: "FAILED (failures=N)" or "OK".
fn parse_python_test_results(output: &str) -> (usize, usize) {
    let total = parse_python_test_count(output).unwrap_or(0);
    if output.contains("OK") && !output.contains("FAILED") {
        return (total, 0);
    }
    let re_fail = Regex::new(r"failures=(\d+)").ok();
    let failures = re_fail
        .and_then(|re| re.captures(output))
        .and_then(|cap| cap[1].parse::<usize>().ok())
        .unwrap_or(0);
    let re_errors = Regex::new(r"errors=(\d+)").ok();
    let errors = re_errors
        .and_then(|re| re.captures(output))
        .and_then(|cap| cap[1].parse::<usize>().ok())
        .unwrap_or(0);
    let failed = failures + errors;
    (total.saturating_sub(failed), failed)
}

/// Backward-compatible wrapper for Rust code extraction.
#[cfg(test)]
fn extract_rust_code(output: &str) -> Option<String> {
    extract_code(output, Language::Rust)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_extract_rust_code_fenced() {
        let output = "Here is the code:\n```rust\nfn foo() -> bool { true }\n```\nDone.";
        let code = extract_rust_code(output).unwrap();
        assert!(code.contains("fn foo()"));
    }

    #[test]
    fn test_extract_rust_code_generic_fence() {
        let output = "```\nfn bar() {}\n```";
        let code = extract_rust_code(output).unwrap();
        assert!(code.contains("fn bar()"));
    }

    #[test]
    fn test_extract_rust_code_no_fence() {
        let output = "pub fn baz(x: i32) -> i32 {\n    x + 1\n}";
        let code = extract_rust_code(output).unwrap();
        assert!(code.contains("fn baz"));
    }

    /// Verify static regexes compile successfully.
    /// Catches any future regex mutation that would otherwise panic at runtime.
    #[test]
    fn regexes_compile() {
        // Force initialisation of the LazyLock; panics here rather than at runtime if invalid.
        let _ = &*RE_GO_MAIN;
        assert!(RE_GO_MAIN.is_match("func main() { return }"));
        assert!(!RE_GO_MAIN.is_match("func helper() { return }"));
    }
}
