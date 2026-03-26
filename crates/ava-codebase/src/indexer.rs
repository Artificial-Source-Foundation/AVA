use std::collections::HashSet;
use std::path::{Path, PathBuf};
use std::sync::LazyLock;

use regex::Regex;
use tokio::fs;

use crate::graph::DependencyGraph;
use crate::pagerank::{calculate_pagerank, calculate_symbol_pagerank};
use crate::search::SearchIndex;
use crate::symbol_graph::SymbolGraph;
use crate::symbols::{extract_symbols, Symbol, SymbolRef};
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

static RUST_USE_RE: LazyLock<Regex> =
    LazyLock::new(|| Regex::new(r"use\s+(\w+(?:::\w+)*)").expect("valid rust use regex"));
static JS_IMPORT_RE: LazyLock<Regex> = LazyLock::new(|| {
    Regex::new(r#"import\s+.*?\s+from\s+['"]([^'"]+)['"]"#).expect("valid js import regex")
});
static JS_REQUIRE_RE: LazyLock<Regex> = LazyLock::new(|| {
    Regex::new(r#"require\(\s*['"]([^'"]+)['"]\s*\)"#).expect("valid js require regex")
});
static PY_IMPORT_RE: LazyLock<Regex> =
    LazyLock::new(|| Regex::new(r"^import\s+(\S+)").expect("valid py import regex"));
static PY_FROM_RE: LazyLock<Regex> =
    LazyLock::new(|| Regex::new(r"^from\s+(\S+)\s+import").expect("valid py from regex"));

/// Index a project directory, building a search index, dependency graph, and PageRank scores.
pub async fn index_project(root: &Path) -> Result<CodebaseIndex> {
    let roots = vec![root.to_path_buf()];
    index_roots(&roots, false).await
}

/// Index multiple project roots into one composite index.
/// Result paths are repo-qualified as `<repo_name>:<relative_path>`.
pub async fn index_workspace(roots: &[PathBuf]) -> Result<CodebaseIndex> {
    index_roots(roots, true).await
}

async fn index_roots(roots: &[PathBuf], qualify_repo_paths: bool) -> Result<CodebaseIndex> {
    let search = SearchIndex::new()?;
    let mut graph = DependencyGraph::new();
    let mut all_symbols: Vec<Symbol> = Vec::new();
    let mut all_refs: Vec<SymbolRef> = Vec::new();
    #[cfg(feature = "semantic")]
    let mut semantic = crate::semantic::SemanticIndex::new();

    let mut repo_names = HashSet::new();
    for (idx, root) in roots.iter().enumerate() {
        if !root.is_dir() {
            continue;
        }
        let repo_name = repo_label(root, idx + 1, &mut repo_names);
        index_single_root(
            root,
            &repo_name,
            qualify_repo_paths,
            &search,
            &mut graph,
            &mut all_symbols,
            &mut all_refs,
            #[cfg(feature = "semantic")]
            &mut semantic,
        )
        .await;
    }

    search.commit()?;
    let pagerank = calculate_pagerank(&graph, 0.85, 20);

    // Build symbol-level graph and PageRank
    let symbol_graph = SymbolGraph::build(&all_symbols, &all_refs);
    let symbol_pagerank = calculate_symbol_pagerank(&symbol_graph, 0.85, 20);
    tracing::info!(
        "Codebase indexed: {} files, {} symbols, {} symbol edges",
        graph.node_count(),
        symbol_graph.node_count(),
        symbol_graph.edge_count(),
    );

    Ok(CodebaseIndex {
        search,
        graph,
        pagerank,
        symbol_graph: Some(symbol_graph),
        symbol_pagerank,
        #[cfg(feature = "semantic")]
        semantic: Some(semantic),
    })
}

async fn index_single_root(
    root: &Path,
    repo_name: &str,
    qualify_repo_paths: bool,
    search: &SearchIndex,
    graph: &mut DependencyGraph,
    all_symbols: &mut Vec<Symbol>,
    all_refs: &mut Vec<SymbolRef>,
    #[cfg(feature = "semantic")] semantic: &mut crate::semantic::SemanticIndex,
) {
    let mut stack = vec![root.to_path_buf()];
    while let Some(dir) = stack.pop() {
        let Ok(mut entries) = fs::read_dir(&dir).await else {
            continue;
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

            let Ok(content) = fs::read_to_string(&path).await else {
                continue;
            };

            let relative = path
                .strip_prefix(root)
                .unwrap_or(&path)
                .to_string_lossy()
                .to_string();

            let qualified_path = if qualify_repo_paths {
                format!("{repo_name}:{relative}")
            } else {
                relative
            };

            if search
                .add_document(&SearchDocument::new(&qualified_path, &content))
                .is_err()
            {
                continue;
            }
            graph.add_file(&qualified_path);
            #[cfg(feature = "semantic")]
            semantic.add_document(&qualified_path, &content);

            let imports = parse_imports(&content, ext);
            for imp in imports {
                graph.add_dependency(&qualified_path, &imp);
            }

            // Extract symbols for the symbol-level graph
            let (syms, refs) = extract_symbols(&qualified_path, &content, ext);
            all_symbols.extend(syms);
            all_refs.extend(refs);
        }
    }
}

fn repo_label(root: &Path, ordinal: usize, used: &mut HashSet<String>) -> String {
    let base = root
        .file_name()
        .and_then(|name| name.to_str())
        .filter(|name| !name.is_empty())
        .unwrap_or("repo");

    if used.insert(base.to_string()) {
        return base.to_string();
    }

    let mut suffix = ordinal;
    loop {
        let candidate = format!("{base}-{suffix}");
        if used.insert(candidate.clone()) {
            return candidate;
        }
        suffix += 1;
    }
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
    RUST_USE_RE
        .captures_iter(content)
        .filter_map(|cap| {
            let path = cap.get(1)?.as_str();
            let crate_name = path.split("::").next()?;
            if matches!(
                crate_name,
                "std" | "core" | "alloc" | "self" | "super" | "crate"
            ) {
                return None;
            }
            Some(crate_name.to_string())
        })
        .collect()
}

fn parse_js_imports(content: &str) -> Vec<String> {
    let mut imports = Vec::new();
    for cap in JS_IMPORT_RE.captures_iter(content) {
        if let Some(m) = cap.get(1) {
            imports.push(m.as_str().to_string());
        }
    }
    for cap in JS_REQUIRE_RE.captures_iter(content) {
        if let Some(m) = cap.get(1) {
            imports.push(m.as_str().to_string());
        }
    }
    imports
}

fn parse_python_imports(content: &str) -> Vec<String> {
    let mut imports = Vec::new();
    for line in content.lines() {
        let trimmed = line.trim();
        if let Some(cap) = PY_IMPORT_RE.captures(trimmed) {
            if let Some(m) = cap.get(1) {
                imports.push(m.as_str().to_string());
            }
        }
        if let Some(cap) = PY_FROM_RE.captures(trimmed) {
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
        assert!(index.symbol_pagerank.is_empty());
        assert!(index.symbol_graph.as_ref().unwrap().node_count() == 0);
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

        // Symbol graph should be populated
        let sg = index
            .symbol_graph
            .as_ref()
            .expect("symbol graph should be present");
        assert!(
            sg.node_count() >= 2,
            "symbol graph should have at least 2 symbols, got {}",
            sg.node_count()
        );
        assert!(
            !index.symbol_pagerank.is_empty(),
            "symbol pagerank should be populated"
        );

        // Search should find the files
        let hits = index
            .search
            .search(&crate::SearchQuery::new("hello"))
            .unwrap();
        assert!(!hits.is_empty());
        assert!(hits[0].path.ends_with("src/main.rs") || hits[0].path.ends_with("src/lib.rs"));
    }

    #[tokio::test]
    async fn index_builds_symbol_graph_with_cross_file_refs() {
        let dir = tempfile::tempdir().unwrap();
        let root = dir.path();

        fs::write(
            root.join("types.rs"),
            "pub struct Config {}\npub struct Parser {}\n",
        )
        .await
        .unwrap();
        fs::write(
            root.join("engine.rs"),
            "pub fn run(config: Config, parser: Parser) {}\n",
        )
        .await
        .unwrap();

        let index = index_project(root).await.unwrap();
        let sg = index.symbol_graph.as_ref().unwrap();

        // types.rs should have Config and Parser
        let type_syms = sg.symbols_in_file("types.rs");
        let type_names: Vec<&str> = type_syms.iter().map(|s| s.name.as_str()).collect();
        assert!(
            type_names.contains(&"Config"),
            "types.rs should have Config: {type_names:?}"
        );
        assert!(
            type_names.contains(&"Parser"),
            "types.rs should have Parser: {type_names:?}"
        );

        // engine.rs references Config and Parser, so should have edges
        assert!(
            sg.edge_count() > 0,
            "should have cross-file reference edges"
        );

        // symbol_file_scores should work
        let file_scores = index.symbol_file_scores("");
        assert!(!file_scores.is_empty(), "file scores should not be empty");
    }

    #[tokio::test]
    async fn workspace_index_qualifies_repo_paths() {
        let dir_a = tempfile::tempdir().unwrap();
        let dir_b = tempfile::tempdir().unwrap();

        fs::write(dir_a.path().join("a.rs"), "fn alpha() {} // token_alpha")
            .await
            .unwrap();
        fs::write(dir_b.path().join("b.rs"), "fn beta() {} // token_beta")
            .await
            .unwrap();

        let roots = vec![dir_a.path().to_path_buf(), dir_b.path().to_path_buf()];
        let index = index_workspace(&roots).await.unwrap();
        let hits = index
            .search
            .search(&crate::SearchQuery::new("token_beta"))
            .unwrap();

        assert!(!hits.is_empty());
        assert!(hits.iter().any(|hit| hit.path.contains(':')));
    }

    #[tokio::test]
    async fn workspace_search_spans_multiple_roots() {
        let dir_a = tempfile::tempdir().unwrap();
        let dir_b = tempfile::tempdir().unwrap();

        fs::write(dir_a.path().join("lib.rs"), "shared_token alpha")
            .await
            .unwrap();
        fs::write(dir_b.path().join("main.rs"), "shared_token beta")
            .await
            .unwrap();

        let roots = vec![dir_a.path().to_path_buf(), dir_b.path().to_path_buf()];
        let index = index_workspace(&roots).await.unwrap();
        let hits = index
            .search
            .search(&crate::SearchQuery::new("shared_token").with_max_results(10))
            .unwrap();

        assert!(hits.len() >= 2);
    }

    /// Force evaluation of all `LazyLock<Regex>` statics in this module so that
    /// a malformed pattern causes an immediate panic at test time rather than
    /// a silent failure at runtime.
    #[test]
    fn regexes_compile() {
        let _ = &*RUST_USE_RE;
        let _ = &*JS_IMPORT_RE;
        let _ = &*JS_REQUIRE_RE;
        let _ = &*PY_IMPORT_RE;
        let _ = &*PY_FROM_RE;
    }

    #[cfg(feature = "semantic")]
    #[tokio::test]
    async fn semantic_workspace_index_builds_semantic_store() {
        let dir = tempfile::tempdir().unwrap();
        fs::write(
            dir.path().join("semantic.rs"),
            "vector embedding retrieval for search relevance",
        )
        .await
        .unwrap();

        let roots = vec![dir.path().to_path_buf()];
        let index = index_workspace(&roots).await.unwrap();
        let semantic_hits = index
            .semantic
            .as_ref()
            .expect("semantic index")
            .search("embedding retrieval", 5);

        assert!(!semantic_hits.is_empty());
    }
}
