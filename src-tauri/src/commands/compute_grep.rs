use globset::{Glob, GlobMatcher};
use regex::Regex;
use serde::{Deserialize, Serialize};
use std::fs;
use std::path::Path;
use walkdir::{DirEntry, WalkDir};

const MAX_LINE_LENGTH: usize = 2000;
const DEFAULT_MAX_RESULTS: usize = 100;

#[derive(Debug, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ComputeGrepInput {
    pub path: String,
    pub pattern: String,
    pub include: Option<String>,
    pub max_results: Option<usize>,
}

#[derive(Debug, Serialize)]
pub struct ComputeGrepMatch {
    pub file: String,
    pub line: usize,
    pub content: String,
}

#[derive(Debug, Serialize)]
pub struct ComputeGrepOutput {
    pub matches: Vec<ComputeGrepMatch>,
    pub truncated: bool,
}

#[tauri::command]
pub fn compute_grep(input: ComputeGrepInput) -> Result<ComputeGrepOutput, String> {
    let regex = Regex::new(&input.pattern)
        .map_err(|e| format!("Invalid regex pattern '{}': {}", input.pattern, e))?;

    let include_matcher = build_matcher(input.include.as_deref())?;
    let mut matches = Vec::new();
    let mut truncated = false;
    let max_results = input.max_results.unwrap_or(DEFAULT_MAX_RESULTS);

    let walker = WalkDir::new(&input.path)
        .into_iter()
        .filter_entry(|entry| should_descend(entry));

    for entry in walker {
        let entry = match entry {
            Ok(v) => v,
            Err(_) => continue,
        };

        if !entry.file_type().is_file() {
            continue;
        }

        let path = entry.path();
        if is_binary_extension(path) {
            continue;
        }

        if let Some(matcher) = &include_matcher {
            let file_name = entry.file_name().to_string_lossy();
            let relative_path = path.strip_prefix(Path::new(&input.path)).unwrap_or(path);
            let relative = relative_path.to_string_lossy().replace('\\', "/");
            let matches_file_name = matcher.is_match(file_name.as_ref());
            let matches_relative = matcher.is_match(relative.as_str());
            if !matches_file_name && !matches_relative {
                continue;
            }
        }

        let content = match fs::read_to_string(path) {
            Ok(v) => v,
            Err(_) => continue,
        };

        for (idx, line) in content.lines().enumerate() {
            if matches.len() >= max_results {
                truncated = true;
                break;
            }

            if regex.is_match(line) {
                matches.push(ComputeGrepMatch {
                    file: path.to_string_lossy().to_string(),
                    line: idx + 1,
                    content: truncate(line.trim(), MAX_LINE_LENGTH),
                });
            }
        }

        if truncated {
            break;
        }
    }

    Ok(ComputeGrepOutput { matches, truncated })
}

fn should_descend(entry: &DirEntry) -> bool {
    if entry.depth() == 0 {
        return true;
    }

    if !entry.file_type().is_dir() {
        return true;
    }

    let name = entry.file_name().to_string_lossy();
    if name.starts_with('.') {
        return false;
    }

    !matches!(
        name.as_ref(),
        "node_modules"
            | "__pycache__"
            | "venv"
            | ".venv"
            | "target"
            | "build"
            | "dist"
            | "coverage"
    )
}

fn build_matcher(pattern: Option<&str>) -> Result<Option<GlobMatcher>, String> {
    match pattern {
        Some(p) => {
            let glob = Glob::new(p).map_err(|e| format!("Invalid include glob '{}': {}", p, e))?;
            Ok(Some(glob.compile_matcher()))
        }
        None => Ok(None),
    }
}

fn is_binary_extension(path: &Path) -> bool {
    let ext = path
        .extension()
        .and_then(|e| e.to_str())
        .map(|e| e.to_ascii_lowercase());

    matches!(
        ext.as_deref(),
        Some("png")
            | Some("jpg")
            | Some("jpeg")
            | Some("gif")
            | Some("bmp")
            | Some("ico")
            | Some("webp")
            | Some("svg")
            | Some("mp3")
            | Some("mp4")
            | Some("avi")
            | Some("mov")
            | Some("mkv")
            | Some("flac")
            | Some("wav")
            | Some("ogg")
            | Some("pdf")
            | Some("doc")
            | Some("docx")
            | Some("xls")
            | Some("xlsx")
            | Some("ppt")
            | Some("pptx")
            | Some("zip")
            | Some("tar")
            | Some("gz")
            | Some("bz2")
            | Some("7z")
            | Some("rar")
            | Some("xz")
            | Some("exe")
            | Some("dll")
            | Some("so")
            | Some("dylib")
            | Some("bin")
            | Some("o")
            | Some("a")
            | Some("wasm")
            | Some("pyc")
            | Some("class")
            | Some("jar")
            | Some("ttf")
            | Some("otf")
            | Some("woff")
            | Some("woff2")
            | Some("eot")
            | Some("sqlite")
            | Some("db")
            | Some("sqlite3")
    )
}

fn truncate(input: &str, max_len: usize) -> String {
    if input.chars().count() <= max_len {
        return input.to_string();
    }
    input
        .chars()
        .take(max_len.saturating_sub(3))
        .collect::<String>()
        + "..."
}
