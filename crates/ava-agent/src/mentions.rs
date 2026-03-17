//! @-mention syntax parser for referencing tools, files, and URLs in user input.
//!
//! Parses `@mentions` from user messages to explicitly reference:
//! - Files: `@path/to/file.rs`, `@./relative/path`
//! - Tools: `@read`, `@bash`, `@edit`
//! - URLs: `@https://example.com`

use std::path::{Path, PathBuf};

/// Known built-in tool names that can be referenced via @mention.
const KNOWN_TOOLS: &[&str] = &[
    "read",
    "write",
    "edit",
    "bash",
    "glob",
    "grep",
    "web_fetch",
    "web_search",
    "apply_patch",
    "multiedit",
    "test_runner",
    "lint",
    "diagnostics",
    "git",
    "code_search",
    "task",
    "todo_read",
    "todo_write",
    "question",
];

/// A parsed @mention from user input.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Mention {
    /// A file path reference: `@src/main.rs`, `@./lib.rs`
    File(PathBuf),
    /// A tool name reference: `@read`, `@bash`
    Tool(String),
    /// A URL reference: `@https://example.com`
    Url(String),
}

/// Parse all @mentions from user input.
///
/// Returns `(cleaned_input, mentions)` where `cleaned_input` has the @mentions
/// removed and `mentions` contains all parsed references.
///
/// Rules:
/// - `@path/to/file` or `@./relative/path` -> `Mention::File`
/// - `@toolname` (matching known tools) -> `Mention::Tool`
/// - `@https://...` or `@http://...` -> `Mention::Url`
/// - Email addresses (`user@domain`) are ignored
/// - @mentions inside backtick code spans/blocks are ignored
pub fn parse_mentions(input: &str) -> (String, Vec<Mention>) {
    let mut mentions = Vec::new();
    let mut cleaned = String::with_capacity(input.len());

    // Process character by character, tracking code block state
    let chars: Vec<char> = input.chars().collect();
    let len = chars.len();
    let mut i = 0;

    // Track whether we're inside a code block
    let mut in_code_fence = false;
    let mut in_inline_code = false;

    while i < len {
        // Check for code fences (```)
        if i + 2 < len && chars[i] == '`' && chars[i + 1] == '`' && chars[i + 2] == '`' {
            in_code_fence = !in_code_fence;
            cleaned.push('`');
            cleaned.push('`');
            cleaned.push('`');
            i += 3;
            continue;
        }

        // Check for inline code (single backtick, but not inside a fence)
        if !in_code_fence && chars[i] == '`' {
            in_inline_code = !in_inline_code;
            cleaned.push('`');
            i += 1;
            continue;
        }

        // If inside code, pass through
        if in_code_fence || in_inline_code {
            cleaned.push(chars[i]);
            i += 1;
            continue;
        }

        // Check for @ symbol
        if chars[i] == '@' {
            // Check for email: if preceded by a word character, this is email
            if i > 0 && is_word_char(chars[i - 1]) {
                cleaned.push(chars[i]);
                i += 1;
                continue;
            }

            // Extract the mention text after @
            let start = i + 1;
            if start >= len {
                cleaned.push('@');
                i += 1;
                continue;
            }

            // Collect mention content
            let mention_text = extract_mention_text(&chars, start);
            if mention_text.is_empty() {
                cleaned.push('@');
                i += 1;
                continue;
            }

            // Classify the mention
            if let Some(mention) = classify_mention(&mention_text) {
                mentions.push(mention);
                // Skip the @mention in output; trim trailing whitespace gap
                i = start + mention_text.len();
                // Consume a single trailing space so we don't leave double spaces
                if i < len && chars[i] == ' ' {
                    i += 1;
                }
            } else {
                // Not a valid mention, keep the @
                cleaned.push('@');
                i += 1;
            }
        } else {
            cleaned.push(chars[i]);
            i += 1;
        }
    }

    // Trim trailing whitespace that may result from mention removal at end
    let cleaned = cleaned.trim_end_matches(' ').to_string();

    (cleaned, mentions)
}

/// Resolve a file mention path against a working directory.
///
/// Returns `Some(canonical_path)` if the file exists, `None` otherwise.
pub fn resolve_file_mention(mention: &Path, cwd: &Path) -> Option<PathBuf> {
    let path = if mention.is_absolute() {
        mention.to_path_buf()
    } else {
        cwd.join(mention)
    };

    if path.exists() {
        path.canonicalize().ok()
    } else {
        None
    }
}

/// Check if a character is a word character (alphanumeric or underscore).
fn is_word_char(c: char) -> bool {
    c.is_alphanumeric() || c == '_'
}

/// Extract mention text starting at position `start` in the char slice.
/// Mention text can contain alphanumeric, `/`, `.`, `-`, `_`, `:`, `~`, `#`, `?`, `=`, `&`, `%`.
fn extract_mention_text(chars: &[char], start: usize) -> String {
    let mut end = start;
    let len = chars.len();

    while end < len {
        let c = chars[end];
        if c.is_alphanumeric()
            || c == '/'
            || c == '.'
            || c == '-'
            || c == '_'
            || c == ':'
            || c == '~'
            || c == '#'
            || c == '?'
            || c == '='
            || c == '&'
            || c == '%'
        {
            end += 1;
        } else {
            break;
        }
    }

    chars[start..end].iter().collect()
}

/// Classify a mention string into a `Mention` variant.
fn classify_mention(text: &str) -> Option<Mention> {
    if text.is_empty() {
        return None;
    }

    // URL mention: starts with http:// or https://
    if text.starts_with("https://") || text.starts_with("http://") {
        return Some(Mention::Url(text.to_string()));
    }

    // Tool mention: exact match against known tool names
    if KNOWN_TOOLS.contains(&text) {
        return Some(Mention::Tool(text.to_string()));
    }

    // File mention: contains a path separator, starts with `.`, or has a file extension
    if text.contains('/')
        || text.starts_with('.')
        || text.starts_with('~')
        || has_file_extension(text)
    {
        return Some(Mention::File(PathBuf::from(text)));
    }

    None
}

/// Check if text looks like it has a file extension (e.g., `main.rs`, `config.toml`).
fn has_file_extension(text: &str) -> bool {
    if let Some(dot_pos) = text.rfind('.') {
        let ext = &text[dot_pos + 1..];
        // Extension should be 1-10 alphanumeric characters
        !ext.is_empty() && ext.len() <= 10 && ext.chars().all(|c| c.is_alphanumeric())
    } else {
        false
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_file_mention_with_path() {
        let (cleaned, mentions) = parse_mentions("Look at @src/main.rs please");
        assert_eq!(cleaned, "Look at please");
        assert_eq!(mentions, vec![Mention::File(PathBuf::from("src/main.rs"))]);
    }

    #[test]
    fn test_file_mention_relative() {
        let (cleaned, mentions) = parse_mentions("Check @./lib.rs");
        assert_eq!(cleaned, "Check");
        assert_eq!(mentions, vec![Mention::File(PathBuf::from("./lib.rs"))]);
    }

    #[test]
    fn test_file_mention_with_extension() {
        let (cleaned, mentions) = parse_mentions("Read @Cargo.toml");
        assert_eq!(cleaned, "Read");
        assert_eq!(mentions, vec![Mention::File(PathBuf::from("Cargo.toml"))]);
    }

    #[test]
    fn test_tool_mention() {
        let (cleaned, mentions) = parse_mentions("Use @bash to run it");
        assert_eq!(cleaned, "Use to run it");
        assert_eq!(mentions, vec![Mention::Tool("bash".to_string())]);
    }

    #[test]
    fn test_tool_mention_various() {
        for tool in &["read", "write", "edit", "glob", "grep", "web_fetch"] {
            let input = format!("use @{tool}");
            let (_, mentions) = parse_mentions(&input);
            assert_eq!(mentions, vec![Mention::Tool(tool.to_string())]);
        }
    }

    #[test]
    fn test_url_mention() {
        let (cleaned, mentions) = parse_mentions("Fetch @https://example.com/api");
        assert_eq!(cleaned, "Fetch");
        assert_eq!(
            mentions,
            vec![Mention::Url("https://example.com/api".to_string())]
        );
    }

    #[test]
    fn test_url_mention_http() {
        let (_, mentions) = parse_mentions("See @http://localhost:8080/health");
        assert_eq!(
            mentions,
            vec![Mention::Url("http://localhost:8080/health".to_string())]
        );
    }

    #[test]
    fn test_email_exclusion() {
        let (cleaned, mentions) = parse_mentions("Contact user@example.com for help");
        assert_eq!(cleaned, "Contact user@example.com for help");
        assert!(mentions.is_empty());
    }

    #[test]
    fn test_code_block_exclusion() {
        let input = "Look at ```@bash``` and @read";
        let (cleaned, mentions) = parse_mentions(input);
        assert_eq!(cleaned, "Look at ```@bash``` and");
        // Only @read is parsed, @bash is inside code fence
        assert_eq!(mentions, vec![Mention::Tool("read".to_string())]);
    }

    #[test]
    fn test_inline_code_exclusion() {
        let input = "Run `@bash echo hi` and also @grep";
        let (cleaned, mentions) = parse_mentions(input);
        assert_eq!(cleaned, "Run `@bash echo hi` and also");
        assert_eq!(mentions, vec![Mention::Tool("grep".to_string())]);
    }

    #[test]
    fn test_multiple_mentions() {
        let input = "Check @src/lib.rs and @Cargo.toml using @read";
        let (cleaned, mentions) = parse_mentions(input);
        assert_eq!(cleaned, "Check and using");
        assert_eq!(
            mentions,
            vec![
                Mention::File(PathBuf::from("src/lib.rs")),
                Mention::File(PathBuf::from("Cargo.toml")),
                Mention::Tool("read".to_string()),
            ]
        );
    }

    #[test]
    fn test_no_mentions() {
        let (cleaned, mentions) = parse_mentions("Just a normal message");
        assert_eq!(cleaned, "Just a normal message");
        assert!(mentions.is_empty());
    }

    #[test]
    fn test_mixed_content() {
        let input =
            "Read @src/main.rs with @read and fetch @https://docs.rs then email me at dev@ava.com";
        let (cleaned, mentions) = parse_mentions(input);
        assert_eq!(cleaned, "Read with and fetch then email me at dev@ava.com");
        assert_eq!(
            mentions,
            vec![
                Mention::File(PathBuf::from("src/main.rs")),
                Mention::Tool("read".to_string()),
                Mention::Url("https://docs.rs".to_string()),
            ]
        );
    }

    #[test]
    fn test_mention_at_start() {
        let (cleaned, mentions) = parse_mentions("@bash run tests");
        assert_eq!(cleaned, "run tests");
        assert_eq!(mentions, vec![Mention::Tool("bash".to_string())]);
    }

    #[test]
    fn test_mention_at_end() {
        let (cleaned, mentions) = parse_mentions("Read the file @src/lib.rs");
        assert_eq!(cleaned, "Read the file");
        assert_eq!(mentions, vec![Mention::File(PathBuf::from("src/lib.rs"))]);
    }

    #[test]
    fn test_bare_at_sign() {
        let (cleaned, mentions) = parse_mentions("Just an @ symbol");
        assert_eq!(cleaned, "Just an @ symbol");
        assert!(mentions.is_empty());
    }

    #[test]
    fn test_at_end_of_input() {
        let (cleaned, mentions) = parse_mentions("trailing @");
        assert_eq!(cleaned, "trailing @");
        assert!(mentions.is_empty());
    }

    #[test]
    fn test_unknown_bare_word_ignored() {
        // A bare word that isn't a tool and has no path chars should not match
        let (cleaned, mentions) = parse_mentions("Hello @world");
        assert_eq!(cleaned, "Hello @world");
        assert!(mentions.is_empty());
    }

    #[test]
    fn test_resolve_file_mention_exists() {
        let dir = tempfile::tempdir().unwrap();
        let file = dir.path().join("test.rs");
        std::fs::write(&file, "fn main() {}").unwrap();

        let resolved = resolve_file_mention(Path::new("test.rs"), dir.path());
        assert!(resolved.is_some());
        assert_eq!(resolved.unwrap(), file.canonicalize().unwrap());
    }

    #[test]
    fn test_resolve_file_mention_not_exists() {
        let dir = tempfile::tempdir().unwrap();
        let resolved = resolve_file_mention(Path::new("nonexistent.rs"), dir.path());
        assert!(resolved.is_none());
    }

    #[test]
    fn test_resolve_file_mention_absolute() {
        let dir = tempfile::tempdir().unwrap();
        let file = dir.path().join("abs.rs");
        std::fs::write(&file, "").unwrap();

        let resolved = resolve_file_mention(&file, Path::new("/tmp"));
        assert!(resolved.is_some());
    }

    #[test]
    fn test_nested_path_mention() {
        let (_, mentions) = parse_mentions("Look at @crates/ava-agent/src/mentions.rs");
        assert_eq!(
            mentions,
            vec![Mention::File(PathBuf::from(
                "crates/ava-agent/src/mentions.rs"
            ))]
        );
    }

    #[test]
    fn test_home_dir_mention() {
        let (_, mentions) = parse_mentions("Check @~/.ava/config.toml");
        assert_eq!(
            mentions,
            vec![Mention::File(PathBuf::from("~/.ava/config.toml"))]
        );
    }
}
