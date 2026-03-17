//! Pattern extraction for permission learning.
//!
//! Extracts structural patterns from shell commands so the permission system can
//! learn which commands the user regularly approves and auto-approve similar future
//! commands.

use regex::Regex;

/// A structural pattern extracted from a shell command.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct CommandPattern {
    /// The program name (e.g., "cargo", "npm", "git").
    pub program: String,
    /// The subcommand, if any (e.g., "test", "run", "commit").
    pub subcommand: Option<String>,
    /// A regex that matches equivalent commands.
    pub regex: String,
}

impl CommandPattern {
    /// Check if a command matches this pattern.
    pub fn matches(&self, command: &str) -> bool {
        Regex::new(&self.regex)
            .map(|re| re.is_match(command))
            .unwrap_or(false)
    }
}

/// Well-known programs that use a subcommand as their first argument.
const SUBCOMMAND_PROGRAMS: &[&str] = &[
    "cargo", "npm", "npx", "yarn", "pnpm", "git", "docker", "kubectl", "go", "pip", "pip3",
    "poetry", "pdm", "uv", "rustup", "apt", "brew", "dnf", "pacman", "make",
];

/// Programs that use `-m` flag for module-as-subcommand (e.g., `python -m pytest`).
const MODULE_FLAG_PROGRAMS: &[&str] = &["python", "python3"];

/// Extract a structural pattern from a shell command.
///
/// Parses the command into program + optional subcommand and generates a regex
/// that matches equivalent invocations. Returns `None` for empty or unparseable commands.
///
/// # Examples
///
/// ```
/// use ava_permissions::patterns::extract_pattern;
///
/// let p = extract_pattern("cargo test --workspace").unwrap();
/// assert_eq!(p.program, "cargo");
/// assert_eq!(p.subcommand.as_deref(), Some("test"));
/// assert!(p.matches("cargo test"));
/// assert!(p.matches("cargo test --release"));
/// assert!(!p.matches("cargo build"));
/// ```
pub fn extract_pattern(command: &str) -> Option<CommandPattern> {
    let trimmed = command.trim();
    if trimmed.is_empty() {
        return None;
    }

    let words = shell_words(trimmed);
    if words.is_empty() {
        return None;
    }

    let program = &words[0];

    // Handle `python -m module` pattern
    if MODULE_FLAG_PROGRAMS.contains(&program.as_str()) {
        if let Some(module) = find_module_arg(&words) {
            let regex = format!(
                r"^{}\s+-m\s+{}",
                regex::escape(program),
                regex::escape(&module)
            );
            return Some(CommandPattern {
                program: program.clone(),
                subcommand: Some(module),
                regex,
            });
        }
    }

    // Handle programs with subcommands
    if SUBCOMMAND_PROGRAMS.contains(&program.as_str()) {
        if let Some(subcommand) = find_subcommand(&words) {
            let regex = format!(
                r"^{}\s+{}(\s|$)",
                regex::escape(program),
                regex::escape(&subcommand)
            );
            return Some(CommandPattern {
                program: program.clone(),
                subcommand: Some(subcommand),
                regex,
            });
        }
    }

    // Program-only pattern (no recognized subcommand)
    let regex = format!(r"^{}(\s|$)", regex::escape(program));
    Some(CommandPattern {
        program: program.clone(),
        subcommand: None,
        regex,
    })
}

/// Find the first non-flag argument after the program name (the subcommand).
fn find_subcommand(words: &[String]) -> Option<String> {
    words.iter().skip(1).find(|w| !w.starts_with('-')).cloned()
}

/// Find the module name after a `-m` flag.
fn find_module_arg(words: &[String]) -> Option<String> {
    let mut iter = words.iter().skip(1);
    while let Some(w) = iter.next() {
        if w == "-m" {
            return iter.next().cloned();
        }
    }
    None
}

/// Simple word splitting that respects single and double quotes.
fn shell_words(input: &str) -> Vec<String> {
    let mut words = Vec::new();
    let mut current = String::new();
    let mut in_single = false;
    let mut in_double = false;
    let mut prev_escape = false;

    for ch in input.chars() {
        if prev_escape {
            current.push(ch);
            prev_escape = false;
            continue;
        }

        match ch {
            '\\' if !in_single => {
                prev_escape = true;
            }
            '\'' if !in_double => {
                in_single = !in_single;
            }
            '"' if !in_single => {
                in_double = !in_double;
            }
            ' ' | '\t' if !in_single && !in_double => {
                if !current.is_empty() {
                    words.push(current.clone());
                    current.clear();
                }
            }
            _ => {
                current.push(ch);
            }
        }
    }

    if !current.is_empty() {
        words.push(current);
    }

    words
}

/// A store of approved command patterns for permission learning.
///
/// Maintains a list of patterns extracted from previously approved commands.
/// When a new command is submitted, the store checks if it matches any known pattern.
#[derive(Debug, Clone, Default)]
pub struct PatternStore {
    approved_patterns: Vec<CommandPattern>,
}

impl PatternStore {
    /// Create a new empty pattern store.
    pub fn new() -> Self {
        Self {
            approved_patterns: Vec::new(),
        }
    }

    /// Check if a command matches any approved pattern.
    pub fn matches(&self, command: &str) -> bool {
        self.approved_patterns.iter().any(|p| p.matches(command))
    }

    /// Extract a pattern from the command and add it to the approved list.
    ///
    /// Returns `true` if a pattern was successfully extracted and added.
    /// Deduplicates by (program, subcommand) — won't add the same pattern twice.
    pub fn learn(&mut self, command: &str) -> bool {
        if let Some(pattern) = extract_pattern(command) {
            // Deduplicate by (program, subcommand)
            let dominated = self
                .approved_patterns
                .iter()
                .any(|p| p.program == pattern.program && p.subcommand == pattern.subcommand);

            if !dominated {
                self.approved_patterns.push(pattern);
                return true;
            }
        }
        false
    }

    /// Return the number of approved patterns.
    pub fn len(&self) -> usize {
        self.approved_patterns.len()
    }

    /// Return true if there are no approved patterns.
    pub fn is_empty(&self) -> bool {
        self.approved_patterns.is_empty()
    }

    /// Return a slice of all approved patterns.
    pub fn patterns(&self) -> &[CommandPattern] {
        &self.approved_patterns
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    // === Pattern extraction ===

    #[test]
    fn extract_cargo_test() {
        let p = extract_pattern("cargo test --workspace").unwrap();
        assert_eq!(p.program, "cargo");
        assert_eq!(p.subcommand.as_deref(), Some("test"));
        assert!(p.matches("cargo test"));
        assert!(p.matches("cargo test --release"));
        assert!(!p.matches("cargo build"));
    }

    #[test]
    fn extract_npm_run_test() {
        let p = extract_pattern("npm run test").unwrap();
        assert_eq!(p.program, "npm");
        assert_eq!(p.subcommand.as_deref(), Some("run"));
        assert!(p.matches("npm run test"));
        assert!(p.matches("npm run build"));
        assert!(!p.matches("npm install"));
    }

    #[test]
    fn extract_git_commit() {
        let p = extract_pattern("git commit -m 'initial'").unwrap();
        assert_eq!(p.program, "git");
        assert_eq!(p.subcommand.as_deref(), Some("commit"));
        assert!(p.matches("git commit -m 'other msg'"));
        assert!(p.matches("git commit --amend"));
        assert!(!p.matches("git push"));
    }

    #[test]
    fn extract_python_m_pytest() {
        let p = extract_pattern("python -m pytest tests/").unwrap();
        assert_eq!(p.program, "python");
        assert_eq!(p.subcommand.as_deref(), Some("pytest"));
        assert!(p.matches("python -m pytest"));
        assert!(p.matches("python -m pytest tests/unit/"));
        assert!(!p.matches("python -m mypy src/"));
    }

    #[test]
    fn extract_simple_program() {
        let p = extract_pattern("ls -la").unwrap();
        assert_eq!(p.program, "ls");
        assert_eq!(p.subcommand, None);
        assert!(p.matches("ls"));
        assert!(p.matches("ls -la /tmp"));
    }

    #[test]
    fn extract_empty_returns_none() {
        assert!(extract_pattern("").is_none());
        assert!(extract_pattern("   ").is_none());
    }

    #[test]
    fn extract_docker_build() {
        let p = extract_pattern("docker build -t myimage .").unwrap();
        assert_eq!(p.program, "docker");
        assert_eq!(p.subcommand.as_deref(), Some("build"));
        assert!(p.matches("docker build ."));
        assert!(!p.matches("docker run myimage"));
    }

    #[test]
    fn extract_go_test() {
        let p = extract_pattern("go test ./...").unwrap();
        assert_eq!(p.program, "go");
        assert_eq!(p.subcommand.as_deref(), Some("test"));
        assert!(p.matches("go test ./pkg/..."));
        assert!(!p.matches("go build"));
    }

    // === PatternStore matching ===

    #[test]
    fn store_matches_learned_pattern() {
        let mut store = PatternStore::new();
        store.learn("cargo test --workspace");

        assert!(store.matches("cargo test"));
        assert!(store.matches("cargo test --release"));
        assert!(!store.matches("cargo build"));
    }

    #[test]
    fn store_learns_multiple_patterns() {
        let mut store = PatternStore::new();
        store.learn("cargo test");
        store.learn("cargo build");
        store.learn("npm run test");

        assert!(store.matches("cargo test --workspace"));
        assert!(store.matches("cargo build --release"));
        assert!(store.matches("npm run lint"));
        assert!(!store.matches("git push --force"));
    }

    #[test]
    fn store_deduplicates() {
        let mut store = PatternStore::new();
        assert!(store.learn("cargo test --workspace"));
        assert!(!store.learn("cargo test --release")); // same (cargo, test)

        assert_eq!(store.len(), 1);
    }

    #[test]
    fn store_empty_does_not_match() {
        let store = PatternStore::new();
        assert!(!store.matches("cargo test"));
    }

    #[test]
    fn learning_flow() {
        let mut store = PatternStore::new();
        assert!(store.is_empty());

        // User approves "cargo test --workspace"
        store.learn("cargo test --workspace");
        assert_eq!(store.len(), 1);

        // Next time, "cargo test" is auto-approved
        assert!(store.matches("cargo test"));
        assert!(store.matches("cargo test --lib"));

        // But "cargo publish" is not
        assert!(!store.matches("cargo publish"));

        // User approves "cargo publish"
        store.learn("cargo publish");
        assert_eq!(store.len(), 2);
        assert!(store.matches("cargo publish"));
    }

    // === Shell word splitting ===

    #[test]
    fn shell_words_basic() {
        let words = shell_words("cargo test --workspace");
        assert_eq!(words, vec!["cargo", "test", "--workspace"]);
    }

    #[test]
    fn shell_words_quoted() {
        let words = shell_words("git commit -m 'hello world'");
        assert_eq!(words, vec!["git", "commit", "-m", "hello world"]);
    }

    #[test]
    fn shell_words_double_quoted() {
        let words = shell_words(r#"echo "hello world""#);
        assert_eq!(words, vec!["echo", "hello world"]);
    }

    #[test]
    fn shell_words_escaped_space() {
        let words = shell_words(r"cat hello\ world.txt");
        assert_eq!(words, vec!["cat", "hello world.txt"]);
    }
}
