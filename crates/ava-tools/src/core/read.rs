use std::path::PathBuf;
use std::sync::Arc;

use async_trait::async_trait;
use ava_platform::Platform;
use ava_types::{AvaError, ToolResult};
use serde_json::{json, Value};

use crate::core::hashline::{self, HashlineCache};
use crate::registry::Tool;

/// Default maximum lines returned when no explicit `limit` is provided.
const MAX_LINES_DEFAULT: usize = 2000;

/// Maximum file size we will read (10 MB). Larger files should be inspected
/// via `bash` (e.g. `head`, `tail`, `less`).
const MAX_READ_SIZE: u64 = 10 * 1024 * 1024;

/// Maximum image size for inline base64 encoding (5 MB).
const MAX_IMAGE_SIZE: u64 = 5 * 1024 * 1024;

/// Maximum pages per PDF read request.
const MAX_PDF_PAGES: usize = 20;

/// Image file extensions we recognise for inline base64 encoding.
const IMAGE_EXTENSIONS: &[&str] = &["png", "jpg", "jpeg", "gif", "webp", "bmp", "svg"];

/// Check if a path has an image extension.
fn is_image_path(path: &std::path::Path) -> bool {
    path.extension()
        .and_then(|ext| ext.to_str())
        .map(|ext| IMAGE_EXTENSIONS.contains(&ext.to_lowercase().as_str()))
        .unwrap_or(false)
}

/// Check if a path has a .pdf extension.
fn is_pdf_path(path: &std::path::Path) -> bool {
    path.extension()
        .and_then(|ext| ext.to_str())
        .map(|ext| ext.eq_ignore_ascii_case("pdf"))
        .unwrap_or(false)
}

/// Parse a page range specification like "1-5", "3", "1,3,5-10".
///
/// Returns a list of inclusive (start, end) tuples. Pages are 1-indexed.
pub fn parse_page_ranges(spec: &str) -> Result<Vec<(usize, usize)>, String> {
    let mut ranges = Vec::new();
    for part in spec.split(',') {
        let part = part.trim();
        if part.is_empty() {
            continue;
        }
        if let Some((start_str, end_str)) = part.split_once('-') {
            let start: usize = start_str
                .trim()
                .parse()
                .map_err(|_| format!("invalid page number: {}", start_str.trim()))?;
            let end: usize = end_str
                .trim()
                .parse()
                .map_err(|_| format!("invalid page number: {}", end_str.trim()))?;
            if start == 0 || end == 0 {
                return Err("page numbers must be >= 1".to_string());
            }
            if start > end {
                return Err(format!("invalid range: {start}-{end} (start > end)"));
            }
            ranges.push((start, end));
        } else {
            let page: usize = part
                .parse()
                .map_err(|_| format!("invalid page number: {part}"))?;
            if page == 0 {
                return Err("page numbers must be >= 1".to_string());
            }
            ranges.push((page, page));
        }
    }
    if ranges.is_empty() {
        return Err("empty page range specification".to_string());
    }
    Ok(ranges)
}

/// Count total pages spanned by a set of ranges.
fn total_pages(ranges: &[(usize, usize)]) -> usize {
    ranges.iter().map(|(s, e)| e - s + 1).sum()
}

pub struct ReadTool {
    platform: Arc<dyn Platform>,
    hashline_cache: HashlineCache,
}

impl ReadTool {
    pub fn new(platform: Arc<dyn Platform>, hashline_cache: HashlineCache) -> Self {
        Self {
            platform,
            hashline_cache,
        }
    }
}

#[async_trait]
impl Tool for ReadTool {
    fn name(&self) -> &str {
        "read"
    }

    fn description(&self) -> &str {
        "Read file content with line numbers"
    }

    fn parameters(&self) -> Value {
        json!({
            "type": "object",
            "required": ["path"],
            "properties": {
                "path": { "type": "string" },
                "offset": { "type": "integer", "minimum": 1 },
                "limit": { "type": "integer", "minimum": 1 },
                "hash_lines": {
                    "type": "boolean",
                    "description": "When true, prefix each line with a [hash] anchor for use with hash-anchored edits"
                },
                "pages": {
                    "type": "string",
                    "description": "Page range for PDF files (e.g. '1-5', '3', '1,3,5-10'). Max 20 pages per request."
                }
            }
        })
    }

    fn search_hint(&self) -> &str {
        "read file contents lines offset limit"
    }

    fn activity_description(&self, args: &Value) -> Option<String> {
        let path = args
            .get("file_path")
            .or_else(|| args.get("path"))
            .and_then(Value::as_str)?;
        Some(format!("Reading {path}"))
    }

    async fn execute(&self, args: Value) -> ava_types::Result<ToolResult> {
        let path = args
            .get("path")
            .and_then(Value::as_str)
            .ok_or_else(|| AvaError::ValidationError("missing required field: path".to_string()))?;

        let offset = args.get("offset").and_then(Value::as_u64).unwrap_or(1);
        let limit = args.get("limit").and_then(Value::as_u64);
        let hash_lines = args
            .get("hash_lines")
            .and_then(Value::as_bool)
            .unwrap_or(false);
        let pages_spec = args.get("pages").and_then(Value::as_str);

        tracing::debug!(tool = "read", %path, "executing read tool");

        // Enforce workspace boundaries before reading file contents.
        let file_path = crate::core::path_guard::enforce_workspace_path(path, "read")?;

        if !self.platform.exists(&file_path).await {
            return Err(crate::core::path_suggest::missing_file_error(path, &file_path).await);
        }

        // --- Image handling (F17) ---
        if is_image_path(&file_path) {
            let meta = tokio::fs::metadata(&file_path)
                .await
                .map_err(|e| AvaError::IoError(format!("failed to stat image {path}: {e}")))?;
            if meta.len() > MAX_IMAGE_SIZE {
                return Err(AvaError::ToolError(
                    "Image too large for inline processing (limit: 5MB)".to_string(),
                ));
            }
            let bytes = tokio::fs::read(&file_path)
                .await
                .map_err(|e| AvaError::IoError(format!("failed to read image {path}: {e}")))?;
            use base64::Engine;
            let encoded = base64::engine::general_purpose::STANDARD.encode(&bytes);
            let filename = file_path
                .file_name()
                .and_then(|n| n.to_str())
                .unwrap_or(path);
            let content = format!("[Image: {filename} ({} bytes)]\n{encoded}", meta.len());
            return Ok(ToolResult {
                call_id: String::new(),
                content,
                is_error: false,
            });
        }

        // --- PDF handling (F18) ---
        if is_pdf_path(&file_path) {
            let ranges = if let Some(spec) = pages_spec {
                parse_page_ranges(spec)
                    .map_err(|e| AvaError::ValidationError(format!("invalid page range: {e}")))?
            } else {
                vec![(1, 1)] // default: first page only
            };
            let total = total_pages(&ranges);
            if total > MAX_PDF_PAGES {
                return Err(AvaError::ValidationError(format!(
                    "Too many pages requested ({total}). Maximum {MAX_PDF_PAGES} pages per request."
                )));
            }
            let mut all_output = String::new();
            for (start, end) in &ranges {
                let output = tokio::process::Command::new("pdftotext")
                    .args([
                        "-f",
                        &start.to_string(),
                        "-l",
                        &end.to_string(),
                        file_path.to_str().unwrap_or(path),
                        "-",
                    ])
                    .output()
                    .await
                    .map_err(|e| {
                        if e.kind() == std::io::ErrorKind::NotFound {
                            AvaError::ToolError(
                                "PDF reading requires pdftotext. Install with: apt install poppler-utils".to_string(),
                            )
                        } else {
                            AvaError::IoError(format!("pdftotext failed: {e}"))
                        }
                    })?;
                if !output.status.success() {
                    let stderr = String::from_utf8_lossy(&output.stderr);
                    return Err(AvaError::ToolError(format!("pdftotext failed: {stderr}")));
                }
                let text = String::from_utf8_lossy(&output.stdout);
                for page_num in *start..=*end {
                    if !all_output.is_empty() {
                        all_output.push('\n');
                    }
                    all_output.push_str(&format!("--- Page {page_num} ---\n"));
                }
                all_output.push_str(&text);
            }
            return Ok(ToolResult {
                call_id: String::new(),
                content: all_output,
                is_error: false,
            });
        }

        // Guard against OOM: reject files larger than MAX_READ_SIZE before
        // loading them into memory.
        match tokio::fs::metadata(&file_path).await {
            Ok(meta) if meta.len() > MAX_READ_SIZE => {
                return Err(AvaError::ToolError(format!(
                    "File too large ({:.1}MB). Max: 10MB. Use bash to read portions.",
                    meta.len() as f64 / 1_048_576.0
                )));
            }
            Ok(_) => {}
            // Let the platform read_file handle missing-file / permission errors
            // with its own error mapping below.
            Err(_) => {}
        }

        let content = self
            .platform
            .read_file(&file_path)
            .await
            .map_err(|err| match err {
                AvaError::PermissionDenied(message) | AvaError::IoError(message)
                    if message.contains("Permission denied") =>
                {
                    AvaError::PermissionDenied(format!("permission denied: {path} ({message})"))
                }
                other => other,
            })?;

        // Populate hashline cache when hash_lines is enabled
        if hash_lines {
            let entries = hashline::build_cache(&content);
            if let Ok(mut cache) = self.hashline_cache.write() {
                cache.insert(PathBuf::from(path), entries);
            }
        }

        let start = usize::try_from(offset.saturating_sub(1)).unwrap_or(usize::MAX);
        let mut lines: Vec<String> = content
            .lines()
            .enumerate()
            .skip(start)
            .map(|(idx, line)| {
                if hash_lines {
                    let h = hashline::hash_line(line);
                    format!("{:>6}\t[{h}] {line}", idx + 1)
                } else {
                    format!("{:>6}\t{line}", idx + 1)
                }
            })
            .collect();

        let cap = limit
            .and_then(|l| if l == 0 { None } else { Some(l) })
            .map(|l| usize::try_from(l).unwrap_or(usize::MAX))
            .unwrap_or(MAX_LINES_DEFAULT);
        let truncated = lines.len() > cap;
        lines.truncate(cap);

        let mut content = lines.join("\n");
        if truncated {
            content.push_str(&format!(
                "\n\n[Truncated: showing first {cap} lines. Use offset/limit to read more.]"
            ));
        }

        // If even the truncated-by-lines content is large, save to disk
        let content =
            super::output_fallback::save_tool_output_fallback("read", &content, 100 * 1024);
        let content = super::secret_redaction::redact_secrets(&content);

        Ok(ToolResult {
            call_id: String::new(),
            content,
            is_error: false,
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    // --- F18: parse_page_ranges tests ---

    #[test]
    fn parse_single_page() {
        let ranges = parse_page_ranges("3").unwrap();
        assert_eq!(ranges, vec![(3, 3)]);
    }

    #[test]
    fn parse_page_range() {
        let ranges = parse_page_ranges("1-5").unwrap();
        assert_eq!(ranges, vec![(1, 5)]);
    }

    #[test]
    fn parse_comma_separated() {
        let ranges = parse_page_ranges("1,3,5-10").unwrap();
        assert_eq!(ranges, vec![(1, 1), (3, 3), (5, 10)]);
    }

    #[test]
    fn parse_with_spaces() {
        let ranges = parse_page_ranges(" 2 - 4 , 7 ").unwrap();
        assert_eq!(ranges, vec![(2, 4), (7, 7)]);
    }

    #[test]
    fn parse_invalid_range_start_greater_than_end() {
        let err = parse_page_ranges("5-3").unwrap_err();
        assert!(err.contains("start > end"));
    }

    #[test]
    fn parse_invalid_page_zero() {
        let err = parse_page_ranges("0").unwrap_err();
        assert!(err.contains(">= 1"));
    }

    #[test]
    fn parse_invalid_non_numeric() {
        let err = parse_page_ranges("abc").unwrap_err();
        assert!(err.contains("invalid page number"));
    }

    #[test]
    fn parse_empty_spec() {
        let err = parse_page_ranges("").unwrap_err();
        assert!(err.contains("empty"));
    }

    #[test]
    fn max_pages_exceeded() {
        let ranges = parse_page_ranges("1-25").unwrap();
        let total = total_pages(&ranges);
        assert!(total > MAX_PDF_PAGES);
    }

    // --- F17: image detection tests ---

    #[test]
    fn image_extension_detection() {
        use std::path::Path;
        assert!(is_image_path(Path::new("photo.png")));
        assert!(is_image_path(Path::new("photo.PNG")));
        assert!(is_image_path(Path::new("photo.jpg")));
        assert!(is_image_path(Path::new("photo.jpeg")));
        assert!(is_image_path(Path::new("photo.gif")));
        assert!(is_image_path(Path::new("photo.webp")));
        assert!(is_image_path(Path::new("photo.bmp")));
        assert!(is_image_path(Path::new("photo.svg")));
        assert!(!is_image_path(Path::new("file.txt")));
        assert!(!is_image_path(Path::new("file.rs")));
        assert!(!is_image_path(Path::new("file.pdf")));
    }

    #[test]
    fn pdf_extension_detection() {
        use std::path::Path;
        assert!(is_pdf_path(Path::new("doc.pdf")));
        assert!(is_pdf_path(Path::new("doc.PDF")));
        assert!(!is_pdf_path(Path::new("doc.txt")));
        assert!(!is_pdf_path(Path::new("doc.png")));
    }

    // --- F55: activity_description test for read ---

    #[test]
    fn activity_description_read() {
        use crate::registry::Tool;
        let tool = ReadTool::new(
            Arc::new(ava_platform::StandardPlatform),
            crate::core::hashline::new_cache(),
        );
        let desc = tool.activity_description(&json!({"path": "src/main.rs"}));
        assert_eq!(desc, Some("Reading src/main.rs".to_string()));

        let desc_none = tool.activity_description(&json!({}));
        assert_eq!(desc_none, None);
    }
}
