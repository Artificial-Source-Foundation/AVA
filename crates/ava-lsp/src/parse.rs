use std::path::{Path, PathBuf};

use serde_json::Value;
use url::Url;

use crate::types::{DiagnosticSummary, LspDiagnostic, LspError, LspLocation, Result, SymbolInfo};

pub(crate) fn summarize_diagnostics<'a>(
    iter: impl Iterator<Item = &'a LspDiagnostic>,
) -> DiagnosticSummary {
    let mut summary = DiagnosticSummary::default();
    for diagnostic in iter {
        match diagnostic.severity.as_str() {
            "error" => summary.errors += 1,
            "warning" => summary.warnings += 1,
            _ => summary.info += 1,
        }
    }
    summary
}

pub(crate) fn parse_diagnostics_array(items: Vec<Value>, file_path: &Path) -> Vec<LspDiagnostic> {
    items
        .into_iter()
        .map(|item| LspDiagnostic {
            file: file_path.display().to_string(),
            line: item
                .pointer("/range/start/line")
                .and_then(Value::as_u64)
                .unwrap_or(0) as u32
                + 1,
            column: item
                .pointer("/range/start/character")
                .and_then(Value::as_u64)
                .unwrap_or(0) as u32
                + 1,
            severity: severity_name(item.get("severity").and_then(Value::as_u64)),
            message: item
                .get("message")
                .and_then(Value::as_str)
                .unwrap_or_default()
                .to_string(),
            source: item
                .get("source")
                .and_then(Value::as_str)
                .map(ToString::to_string),
        })
        .collect()
}

pub(crate) fn parse_diagnostic_report(value: &Value, file_path: &Path) -> Vec<LspDiagnostic> {
    value
        .get("items")
        .and_then(Value::as_array)
        .cloned()
        .map(|items| parse_diagnostics_array(items, file_path))
        .unwrap_or_default()
}

pub(crate) fn parse_locations(result: &Value) -> Vec<LspLocation> {
    match result {
        Value::Array(items) => items.iter().filter_map(parse_location_value).collect(),
        Value::Object(_) => parse_location_value(result).into_iter().collect(),
        _ => Vec::new(),
    }
}

pub(crate) fn parse_location_value(value: &Value) -> Option<LspLocation> {
    let uri = value
        .get("uri")
        .or_else(|| value.pointer("/targetUri"))
        .and_then(Value::as_str)
        .and_then(|raw| Url::parse(raw).ok())
        .and_then(|url| url.to_file_path().ok())?;
    let range = value
        .get("range")
        .or_else(|| value.get("targetSelectionRange"))?;
    Some(LspLocation {
        file: uri.display().to_string(),
        line: range
            .pointer("/start/line")
            .and_then(Value::as_u64)
            .unwrap_or(0) as u32
            + 1,
        column: range
            .pointer("/start/character")
            .and_then(Value::as_u64)
            .unwrap_or(0) as u32
            + 1,
        end_line: range
            .pointer("/end/line")
            .and_then(Value::as_u64)
            .unwrap_or(0) as u32
            + 1,
        end_column: range
            .pointer("/end/character")
            .and_then(Value::as_u64)
            .unwrap_or(0) as u32
            + 1,
    })
}

pub(crate) fn parse_hover(value: &Value) -> Option<String> {
    let contents = value.get("contents")?;
    match contents {
        Value::String(text) => Some(text.clone()),
        Value::Array(items) => Some(
            items
                .iter()
                .filter_map(|item| {
                    item.as_str().map(ToString::to_string).or_else(|| {
                        item.get("value")
                            .and_then(Value::as_str)
                            .map(ToString::to_string)
                    })
                })
                .collect::<Vec<_>>()
                .join("\n\n"),
        ),
        Value::Object(map) => map
            .get("value")
            .and_then(Value::as_str)
            .map(ToString::to_string),
        _ => None,
    }
}

pub(crate) fn parse_document_symbols(value: &Value) -> Vec<SymbolInfo> {
    let mut symbols = Vec::new();
    if let Value::Array(items) = value {
        for item in items {
            collect_document_symbol(item, &mut symbols);
        }
    }
    symbols
}

fn collect_document_symbol(value: &Value, out: &mut Vec<SymbolInfo>) {
    let location = value
        .get("location")
        .and_then(parse_location_value)
        .or_else(|| {
            value.get("range").map(|range| LspLocation {
                file: String::new(),
                line: range
                    .pointer("/start/line")
                    .and_then(Value::as_u64)
                    .unwrap_or(0) as u32
                    + 1,
                column: range
                    .pointer("/start/character")
                    .and_then(Value::as_u64)
                    .unwrap_or(0) as u32
                    + 1,
                end_line: range
                    .pointer("/end/line")
                    .and_then(Value::as_u64)
                    .unwrap_or(0) as u32
                    + 1,
                end_column: range
                    .pointer("/end/character")
                    .and_then(Value::as_u64)
                    .unwrap_or(0) as u32
                    + 1,
            })
        });
    out.push(SymbolInfo {
        name: value
            .get("name")
            .and_then(Value::as_str)
            .unwrap_or_default()
            .to_string(),
        kind: symbol_kind_name(value.get("kind").and_then(Value::as_u64)),
        detail: value
            .get("detail")
            .and_then(Value::as_str)
            .map(ToString::to_string),
        location,
    });
    if let Some(children) = value.get("children").and_then(Value::as_array) {
        for child in children {
            collect_document_symbol(child, out);
        }
    }
}

pub(crate) fn parse_workspace_symbols(value: &Value) -> Vec<SymbolInfo> {
    value
        .as_array()
        .into_iter()
        .flatten()
        .map(|item| SymbolInfo {
            name: item
                .get("name")
                .and_then(Value::as_str)
                .unwrap_or_default()
                .to_string(),
            kind: symbol_kind_name(item.get("kind").and_then(Value::as_u64)),
            detail: item
                .get("containerName")
                .and_then(Value::as_str)
                .map(ToString::to_string),
            location: item.get("location").and_then(parse_location_value),
        })
        .collect()
}

fn severity_name(value: Option<u64>) -> String {
    match value.unwrap_or(3) {
        1 => "error",
        2 => "warning",
        3 => "info",
        4 => "hint",
        _ => "info",
    }
    .to_string()
}

fn symbol_kind_name(value: Option<u64>) -> String {
    match value.unwrap_or(13) {
        5 => "class",
        6 => "method",
        12 => "function",
        13 => "variable",
        14 => "constant",
        23 => "struct",
        _ => "symbol",
    }
    .to_string()
}

pub(crate) fn language_id_for_path(path: &Path) -> &'static str {
    match path
        .extension()
        .and_then(|ext| ext.to_str())
        .unwrap_or_default()
    {
        "rs" => "rust",
        "ts" => "typescript",
        "tsx" => "typescriptreact",
        "js" => "javascript",
        "jsx" => "javascriptreact",
        "py" => "python",
        "go" => "go",
        "java" => "java",
        _ => "plaintext",
    }
}

pub(crate) fn file_uri(path: &Path) -> Result<String> {
    Url::from_file_path(path)
        .map(|url| url.to_string())
        .map_err(|_| LspError::RequestFailed(format!("invalid file path: {}", path.display())))
}

pub(crate) fn normalize_path(workspace_root: &Path, path: &Path) -> PathBuf {
    if path.is_absolute() {
        path.to_path_buf()
    } else {
        workspace_root.join(path)
    }
}

pub(crate) fn merge_json(target: &mut Value, extra: Value) {
    match (target, extra) {
        (Value::Object(target), Value::Object(extra)) => {
            for (key, value) in extra {
                merge_json(target.entry(key).or_insert(Value::Null), value);
            }
        }
        (target, extra) => *target = extra,
    }
}
