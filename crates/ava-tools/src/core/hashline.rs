//! Hash-anchored line editing (Hashline).
//!
//! Every line in a file gets a short content-hash anchor when read with
//! `hash_lines: true`. The LLM references these anchors instead of raw text,
//! eliminating "string not found" errors during edits.
//!
//! Hash is the first 6 hex chars of FNV-1a of the trimmed line content,
//! giving 16M possibilities — more than enough for any single file.

use std::collections::HashMap;
use std::path::PathBuf;
use std::sync::{Arc, RwLock};

/// Shared cache mapping file paths to their hashline entries.
///
/// Populated by the read tool when `hash_lines` is true.
/// Consumed by the edit tool to resolve hash anchors before fuzzy matching.
pub type HashlineCache = Arc<RwLock<HashMap<PathBuf, Vec<HashlineEntry>>>>;

/// A single cached line with its hash, 1-based line number, and content.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct HashlineEntry {
    pub hash: String,
    pub line_number: usize,
    pub content: String,
}

/// Create a new empty shared hashline cache.
pub fn new_cache() -> HashlineCache {
    Arc::new(RwLock::new(HashMap::new()))
}

/// FNV-1a hash of the trimmed line content, returned as 6 hex chars.
pub fn hash_line(content: &str) -> String {
    let bytes = content.trim().as_bytes();
    let mut hash: u32 = 0x811c_9dc5; // FNV offset basis
    for &byte in bytes {
        hash ^= u32::from(byte);
        hash = hash.wrapping_mul(0x0100_0193); // FNV prime
    }
    format!("{:06x}", hash & 0x00FF_FFFF) // 6 hex chars = 24 bits
}

/// Annotate each line with a `[hash] ` prefix.
pub fn annotate_lines(content: &str) -> String {
    content
        .lines()
        .map(|line| {
            let h = hash_line(line);
            format!("[{h}] {line}")
        })
        .collect::<Vec<_>>()
        .join("\n")
}

/// Strip `[hash] ` prefixes from annotated content.
pub fn strip_hashes(content: &str) -> String {
    content
        .lines()
        .map(|line| {
            if let Some(rest) = try_strip_hash_prefix(line) {
                rest
            } else {
                line
            }
        })
        .collect::<Vec<_>>()
        .join("\n")
}

/// Try to strip a `[xxxxxx] ` prefix from a line. Returns the remainder if found.
fn try_strip_hash_prefix(line: &str) -> Option<&str> {
    if line.len() < 9 {
        return None;
    }
    let bytes = line.as_bytes();
    if bytes[0] != b'[' || bytes[7] != b']' || bytes[8] != b' ' {
        return None;
    }
    // Validate that chars 1..7 are hex
    for &b in &bytes[1..7] {
        if !b.is_ascii_hexdigit() {
            return None;
        }
    }
    Some(&line[9..])
}

/// Build a hashline cache for a file's content.
///
/// Returns a vec of `(hash, 1-based line number, line content)` entries.
pub fn build_cache(content: &str) -> Vec<HashlineEntry> {
    content
        .lines()
        .enumerate()
        .map(|(idx, line)| HashlineEntry {
            hash: hash_line(line),
            line_number: idx + 1,
            content: line.to_string(),
        })
        .collect()
}

/// Resolve hash anchors in `old_text` using the cache.
///
/// If `old_text` contains lines with `[hash] content` prefixes, look up each
/// hash in the cache. If all hashes resolve and the content matches, return
/// the resolved text (without hash prefixes). If any hash is stale (hash found
/// but content changed), return `Err(StaleFile)`. If no hashes are present,
/// return `None` (caller should fall through to normal matching).
pub fn resolve_anchors(
    old_text: &str,
    cache: &[HashlineEntry],
) -> Result<Option<String>, HashlineError> {
    let lines: Vec<&str> = old_text.lines().collect();
    if lines.is_empty() {
        return Ok(None);
    }

    // Check if any lines have hash prefixes
    let mut has_any_hash = false;
    let mut resolved_lines: Vec<String> = Vec::with_capacity(lines.len());

    for line in &lines {
        if let Some((hash, content_after_hash)) = parse_hash_prefix(line) {
            has_any_hash = true;

            // Look up hash in cache
            let entry = cache.iter().find(|e| e.hash == hash);
            match entry {
                Some(entry) => {
                    // Hash found — verify content hasn't changed
                    let trimmed_cache = entry.content.trim();
                    let trimmed_input = content_after_hash.trim();
                    if !trimmed_input.is_empty() && trimmed_cache != trimmed_input {
                        return Err(HashlineError::StaleFile {
                            hash: hash.to_string(),
                            expected: content_after_hash.to_string(),
                            actual: entry.content.clone(),
                        });
                    }
                    resolved_lines.push(entry.content.clone());
                }
                None => {
                    return Err(HashlineError::HashNotFound(hash.to_string()));
                }
            }
        } else {
            // No hash prefix — pass through as-is
            resolved_lines.push(line.to_string());
        }
    }

    if !has_any_hash {
        return Ok(None);
    }

    Ok(Some(resolved_lines.join("\n")))
}

/// Parse a `[xxxxxx] content` prefix, returning (hash, content_after_hash).
fn parse_hash_prefix(line: &str) -> Option<(&str, &str)> {
    if line.len() < 9 {
        return None;
    }
    let bytes = line.as_bytes();
    if bytes[0] != b'[' || bytes[7] != b']' || bytes[8] != b' ' {
        return None;
    }
    for &b in &bytes[1..7] {
        if !b.is_ascii_hexdigit() {
            return None;
        }
    }
    Some((&line[1..7], &line[9..]))
}

/// Errors from hashline resolution.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum HashlineError {
    /// Hash anchor found in cache but the file content has changed since the read.
    StaleFile {
        hash: String,
        expected: String,
        actual: String,
    },
    /// Hash anchor not found in the cache (file was never read with hash_lines).
    HashNotFound(String),
}

impl std::fmt::Display for HashlineError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::StaleFile {
                hash,
                expected,
                actual,
            } => {
                write!(
                    f,
                    "stale file: hash [{hash}] expected \"{expected}\" but file now has \"{actual}\". \
                     Re-read the file to get fresh hashes."
                )
            }
            Self::HashNotFound(hash) => {
                write!(
                    f,
                    "hash [{hash}] not found in cache. Read the file with hash_lines first."
                )
            }
        }
    }
}

impl std::error::Error for HashlineError {}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn hash_is_deterministic() {
        let h1 = hash_line("fn main() {}");
        let h2 = hash_line("fn main() {}");
        assert_eq!(h1, h2);
    }

    #[test]
    fn hash_is_six_hex_chars() {
        let h = hash_line("hello world");
        assert_eq!(h.len(), 6);
        assert!(h.chars().all(|c| c.is_ascii_hexdigit()));
    }

    #[test]
    fn same_content_same_hash() {
        assert_eq!(hash_line("let x = 42;"), hash_line("let x = 42;"));
    }

    #[test]
    fn trimming_normalizes() {
        assert_eq!(hash_line("  hello  "), hash_line("hello"));
    }

    #[test]
    fn different_content_different_hash() {
        let h1 = hash_line("alpha");
        let h2 = hash_line("beta");
        assert_ne!(h1, h2);
    }

    #[test]
    fn annotate_and_strip_roundtrip() {
        let original = "line one\nline two\nline three";
        let annotated = annotate_lines(original);
        let stripped = strip_hashes(&annotated);
        assert_eq!(stripped, original);
    }

    #[test]
    fn annotate_format() {
        let annotated = annotate_lines("hello");
        let h = hash_line("hello");
        assert_eq!(annotated, format!("[{h}] hello"));
    }

    #[test]
    fn strip_preserves_non_hashed() {
        let input = "no hash here\nalso plain";
        assert_eq!(strip_hashes(input), input);
    }

    #[test]
    fn build_cache_entries() {
        let content = "alpha\nbeta\ngamma";
        let cache = build_cache(content);
        assert_eq!(cache.len(), 3);
        assert_eq!(cache[0].line_number, 1);
        assert_eq!(cache[0].content, "alpha");
        assert_eq!(cache[1].line_number, 2);
        assert_eq!(cache[2].line_number, 3);
        assert_eq!(cache[0].hash, hash_line("alpha"));
    }

    #[test]
    fn resolve_anchors_no_hashes_returns_none() {
        let cache = build_cache("alpha\nbeta");
        let result = resolve_anchors("alpha\nbeta", &cache).unwrap();
        assert!(result.is_none());
    }

    #[test]
    fn resolve_anchors_with_valid_hashes() {
        let content = "fn main() {\n    println!(\"hi\");\n}";
        let cache = build_cache(content);
        let h0 = hash_line("fn main() {");
        let h1 = hash_line("    println!(\"hi\");");
        let old_text = format!("[{h0}] fn main() {{\n[{h1}]     println!(\"hi\");");
        let resolved = resolve_anchors(&old_text, &cache).unwrap().unwrap();
        assert_eq!(resolved, "fn main() {\n    println!(\"hi\");");
    }

    #[test]
    fn resolve_anchors_stale_file() {
        let cache = build_cache("original line");
        let h = hash_line("original line");
        let old_text = format!("[{h}] changed line");
        let result = resolve_anchors(&old_text, &cache);
        assert!(result.is_err());
        match result.unwrap_err() {
            HashlineError::StaleFile { hash, .. } => assert_eq!(hash, h),
            other => panic!("expected StaleFile, got {other:?}"),
        }
    }

    #[test]
    fn resolve_anchors_hash_not_found() {
        let cache = build_cache("some content");
        let old_text = "[abcdef] anything";
        let result = resolve_anchors(old_text, &cache);
        // abcdef likely won't match the hash of "some content"
        assert!(result.is_err());
    }

    #[test]
    fn resolve_anchors_hash_only_no_content() {
        // When the LLM sends just the hash with empty content after it, we resolve from cache
        let content = "fn foo() {}";
        let cache = build_cache(content);
        let h = hash_line("fn foo() {}");
        let old_text = format!("[{h}] ");
        // Empty content after hash — should resolve to cached content
        let resolved = resolve_anchors(&old_text, &cache).unwrap().unwrap();
        assert_eq!(resolved, "fn foo() {}");
    }

    #[test]
    fn mixed_hashed_and_plain_lines() {
        let content = "line a\nline b\nline c";
        let cache = build_cache(content);
        let h = hash_line("line b");
        let old_text = format!("line a\n[{h}] line b\nline c");
        let resolved = resolve_anchors(&old_text, &cache).unwrap().unwrap();
        assert_eq!(resolved, "line a\nline b\nline c");
    }

    #[test]
    fn parse_hash_prefix_valid() {
        let (hash, content) = parse_hash_prefix("[a1b2c3] hello world").unwrap();
        assert_eq!(hash, "a1b2c3");
        assert_eq!(content, "hello world");
    }

    #[test]
    fn parse_hash_prefix_invalid_no_bracket() {
        assert!(parse_hash_prefix("a1b2c3] hello").is_none());
    }

    #[test]
    fn parse_hash_prefix_non_hex() {
        assert!(parse_hash_prefix("[ghijkl] hello").is_none());
    }

    #[test]
    fn parse_hash_prefix_too_short() {
        assert!(parse_hash_prefix("[abc]").is_none());
    }
}
