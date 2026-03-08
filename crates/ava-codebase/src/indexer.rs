use std::path::Path;

use regex::Regex;
use tokio::fs;

use crate::graph::DependencyGraph;
use crate::pagerank::calculate_pagerank;
use crate::search::SearchIndex;
use crate::types::SearchDocument;
use crate::{CodebaseIndex, Result};

const SKIP_DIRS: &[&str] = &[
    ".git",
    "node_modules",
    "target",
    "dist",
    "build",
    ".next",
    "__pycache__",
    ".venv",
    "vendor",
];

const SOURCE_EXTENSIONS: &[&str] = &[
    "rs", "ts", "js", "py", "go", "java", "tsx", "jsx", "c", "cpp", "h", "toml", "yaml", "yml",
    "json", "md",
];

/// Index a project directory, building a search index, dependency graph, and PageRank scores.
pub async fn index_project(root: &Path) -> Result<CodebaseIndex> {
    let search = SearchIndex::new()?;
    let mut graph = DependencyGraph::new();

    let mut stack = vec![root.to_path_buf()];
    while let Some(dir) = stack.pop() {
        let mut entries = match fs::read_dir(&dir).await {
            Ok(entries) => entries,
            Err(_) => continue,
        };

        while let Ok(Some(entry)) = entries.next_entry().await {
            let path = entry.path();
            let file_name = entry.file_name();
            let name = file_name.to_string_lossy();

            if path.is_dir() {
                if !SKIP_DIRS.contains(&name.as_ref()) {
                    stack.push(path);
                }
                continue;
            }

            let ext = path
                .extension()
                .and_then(|e| e.to_str())
                .unwrap_or_default();
            if !SOURCE_EXTENSIONS.contains(&ext) {
                continue;
            }

            let content = match fs::read_to_string(&path).await {
                Ok(c) => c,
                Err(_) => continue,
            };

            let relative = path
                .strip_prefix(root)
                .unwrap_or(&path)
                .to_string_lossy()
                .to_string();

            search.add_document(&SearchDocument::new(&relative, &content))?;
            graph.add_file(&relative);

            let imports = parse_imports(&content, ext);
            for imp in imports {
                graph.add_dependency(&relative, &imp);
            }
        }
    }

    search.commit()?;
    let pagerank = calculate_pagerank(&graph, 0.85, 20);

    Ok(CodebaseIndex {
        search,
        graph,
        pagerank,
    })
}

fn parse_imports(content: &str, ext: &str) -> Vec<String> {
    match ext {
        "rs" => parse_rust_imports(content),
        "ts" | "js" | "tsx" | "jsx" => parse_js_imports(content),
        "py" => parse_python_imports(content),
        _ => Vec::new(),
    }
}

fn parse_rust_imports(content: &str) -> Vec<String> {
    let re = Regex::new(r"use\s+(\w+(?:::\w+)*)").unwrap();
    re.captures_iter(content)
        .filter_map(|cap| {
            let path = cap.get(1)?.as_str();
            let crate_name = path.split("::").next()?;
            if matches!(crate_name, "std" | "core" | "alloc" | "self" | "super" | "crate") {
                return None;
            }
            Some(crate_name.to_string())
        })
        .collect()
}

fn parse_js_imports(content: &str) -> Vec<String> {
    let import_re = Regex::new(r#"import\s+.*?\s+from\s+['"]([^'"]+)['"]"#).unwrap();
    let require_re = Regex::new(r#"require\(\s*['"]([^'"]+)['"]\s*\)"#).unwrap();

    let mut imports = Vec::new();
    for cap in import_re.captures_iter(content) {
        if let Some(m) = cap.get(1) {
            imports.push(m.as_str().to_string());
        }
    }
    for cap in require_re.captures_iter(content) {
        if let Some(m) = cap.get(1) {
            imports.push(m.as_str().to_string());
        }
    }
    imports
}

fn parse_python_imports(content: &str) -> Vec<String> {
    let import_re = Regex::new(r"^import\s+(\S+)").unwrap();
    let from_re = Regex::new(r"^from\s+(\S+)\s+import").unwrap();

    let mut imports = Vec::new();
    for line in content.lines() {
        let trimmed = line.trim();
        if let Some(cap) = import_re.captures(trimmed) {
            if let Some(m) = cap.get(1) {
                imports.push(m.as_str().to_string());
            }
        }
        if let Some(cap) = from_re.captures(trimmed) {
            if let Some(m) = cap.get(1) {
                imports.push(m.as_str().to_string());
            }
        }
    }
    imports
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn rust_imports_parsed() {
        let code = "use ava_tools::registry;\nuse std::collections::HashMap;\nuse crate::foo;";
        let imports = parse_rust_imports(code);
        assert!(imports.contains(&"ava_tools".to_string()));
        assert!(!imports.contains(&"std".to_string()));
        assert!(!imports.contains(&"crate".to_string()));
    }

    #[test]
    fn js_imports_parsed() {
        let code = r#"import { foo } from './bar';
const x = require("lodash");"#;
        let imports = parse_js_imports(code);
        assert!(imports.contains(&"./bar".to_string()));
        assert!(imports.contains(&"lodash".to_string()));
    }

    #[test]
    fn python_imports_parsed() {
        let code = "import os\nfrom pathlib import Path\nimport json";
        let imports = parse_python_imports(code);
        assert!(imports.contains(&"os".to_string()));
        assert!(imports.contains(&"pathlib".to_string()));
        assert!(imports.contains(&"json".to_string()));
    }

    #[tokio::test]
    async fn index_empty_directory() {
        let dir = tempfile::tempdir().unwrap();
        let index = index_project(dir.path()).await.unwrap();
        assert_eq!(index.graph.node_count(), 0);
        assert!(index.pagerank.is_empty());
    }

    #[tokio::test]
    async fn index_temp_directory() {
        let dir = tempfile::tempdir().unwrap();
        let root = dir.path();

        // Create a simple Rust file
        let src_dir = root.join("src");
        fs::create_dir_all(&src_dir).await.unwrap();
        fs::write(
            src_dir.join("main.rs"),
            "use ava_tools::registry;\nfn main() { println!(\"hello\"); }",
        )
        .await
        .unwrap();
        fs::write(
            src_dir.join("lib.rs"),
            "pub mod utils;\npub fn greet() -> String { \"hi\".into() }",
        )
        .await
        .unwrap();

        let index = index_project(root).await.unwrap();
        assert!(index.graph.node_count() >= 2);
        assert!(!index.pagerank.is_empty());

        // Search should find the files
        let hits = index
            .search
            .search(&crate::SearchQuery::new("hello"))
            .unwrap();
        assert!(!hits.is_empty());
    }
}
