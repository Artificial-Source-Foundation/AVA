use std::collections::HashMap;

use crate::graph::DependencyGraph;
use crate::pagerank::{
    calculate_pagerank, calculate_relevance, extract_keywords, find_seed_nodes,
    personalized_pagerank,
};
use crate::symbol_graph::SymbolGraph;
use crate::symbols::{extract_symbols, SymbolKind};

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct RepoFile {
    pub path: String,
    pub content: String,
    pub dependencies: Vec<String>,
}

#[derive(Debug, Clone, PartialEq)]
pub struct RankedFile {
    pub path: String,
    pub score: f64,
}

pub fn generate_repomap(files: &[RepoFile], query: &str) -> Vec<RankedFile> {
    let mut graph = DependencyGraph::new();
    for file in files {
        graph.add_file(file.path.clone());
        for dep in &file.dependencies {
            graph.add_dependency(file.path.clone(), dep.clone());
        }
    }

    let pagerank = calculate_pagerank(&graph, 0.85, 20);
    let keywords = extract_keywords(query);

    let mut ranked = files
        .iter()
        .map(|file| RankedFile {
            path: file.path.clone(),
            score: calculate_relevance(&file.path, &file.content, &keywords, &pagerank),
        })
        .collect::<Vec<_>>();

    ranked.sort_by(|a, b| b.score.total_cmp(&a.score));
    ranked
}

pub fn select_relevant_files(ranked: &[RankedFile], limit: usize) -> Vec<RankedFile> {
    ranked.iter().take(limit).cloned().collect()
}

pub fn score_map(ranked: &[RankedFile]) -> HashMap<String, f64> {
    ranked
        .iter()
        .map(|r| (r.path.clone(), r.score))
        .collect::<HashMap<_, _>>()
}

/// A summary of a symbol for repo map display.
#[derive(Debug, Clone, PartialEq)]
pub struct SymbolSummary {
    pub name: String,
    pub kind: SymbolKind,
    pub line: usize,
    pub score: f64,
}

/// An entry in the repo map: a file with its ranked symbols.
#[derive(Debug, Clone, PartialEq)]
pub struct RepoMapEntry {
    pub path: String,
    pub symbols: Vec<SymbolSummary>,
    pub file_score: f64,
}

/// Generate a symbol-level repo map from source files.
///
/// Uses PageRank on the symbol dependency graph, personalized to the query,
/// to rank files by structural importance + query relevance.
///
/// Returns a tree-formatted string within the given token budget.
/// Token estimation: ~4 chars per token.
pub fn generate_symbol_repomap(
    files: &[(String, String)], // (path, content)
    query: &str,
    token_budget: usize,
) -> String {
    if files.is_empty() {
        return String::new();
    }

    // Phase 1: extract all symbols
    let mut all_symbols = Vec::new();
    let mut all_refs = Vec::new();
    for (path, content) in files {
        let ext = path.rsplit('.').next().unwrap_or("");
        let (syms, refs) = extract_symbols(path, content, ext);
        all_symbols.extend(syms);
        all_refs.extend(refs);
    }

    if all_symbols.is_empty() {
        return String::new();
    }

    // Phase 2: build symbol graph
    let graph = SymbolGraph::build(&all_symbols, &all_refs);

    // Phase 3: run PageRank (personalized if we have a query)
    let symbol_scores = if query.trim().is_empty() {
        crate::pagerank::calculate_symbol_pagerank(&graph, 0.85, 20)
    } else {
        let seeds = find_seed_nodes(&graph, query);
        if seeds.is_empty() {
            crate::pagerank::calculate_symbol_pagerank(&graph, 0.85, 20)
        } else {
            personalized_pagerank(&graph, &seeds, 0.85, 20)
        }
    };

    // Phase 4: aggregate to file scores
    let file_scores = graph.aggregate_file_scores(&symbol_scores);

    // Phase 5: build ranked entries
    let mut entries: Vec<RepoMapEntry> = file_scores
        .iter()
        .map(|(path, &file_score)| {
            let mut symbols: Vec<SymbolSummary> = graph
                .symbols_in_file(path)
                .into_iter()
                .map(|node| SymbolSummary {
                    name: node.name.clone(),
                    kind: node.kind,
                    line: node.line,
                    score: symbol_scores.get(&node.fqn).copied().unwrap_or(0.0),
                })
                .collect();
            symbols.sort_by(|a, b| a.line.cmp(&b.line));
            RepoMapEntry {
                path: path.clone(),
                symbols,
                file_score,
            }
        })
        .collect();

    entries.sort_by(|a, b| b.file_score.total_cmp(&a.file_score));

    // Phase 6: format as tree, respecting token budget
    format_repo_map(&entries, token_budget)
}

/// Get the ranked entries without formatting (useful for programmatic access).
pub fn generate_symbol_repomap_entries(
    files: &[(String, String)],
    query: &str,
) -> Vec<RepoMapEntry> {
    if files.is_empty() {
        return Vec::new();
    }

    let mut all_symbols = Vec::new();
    let mut all_refs = Vec::new();
    for (path, content) in files {
        let ext = path.rsplit('.').next().unwrap_or("");
        let (syms, refs) = extract_symbols(path, content, ext);
        all_symbols.extend(syms);
        all_refs.extend(refs);
    }

    if all_symbols.is_empty() {
        return Vec::new();
    }

    let graph = SymbolGraph::build(&all_symbols, &all_refs);
    let symbol_scores = if query.trim().is_empty() {
        crate::pagerank::calculate_symbol_pagerank(&graph, 0.85, 20)
    } else {
        let seeds = find_seed_nodes(&graph, query);
        if seeds.is_empty() {
            crate::pagerank::calculate_symbol_pagerank(&graph, 0.85, 20)
        } else {
            personalized_pagerank(&graph, &seeds, 0.85, 20)
        }
    };

    let file_scores = graph.aggregate_file_scores(&symbol_scores);

    let mut entries: Vec<RepoMapEntry> = file_scores
        .iter()
        .map(|(path, &file_score)| {
            let mut symbols: Vec<SymbolSummary> = graph
                .symbols_in_file(path)
                .into_iter()
                .map(|node| SymbolSummary {
                    name: node.name.clone(),
                    kind: node.kind,
                    line: node.line,
                    score: symbol_scores.get(&node.fqn).copied().unwrap_or(0.0),
                })
                .collect();
            symbols.sort_by(|a, b| a.line.cmp(&b.line));
            RepoMapEntry {
                path: path.clone(),
                symbols,
                file_score,
            }
        })
        .collect();

    entries.sort_by(|a, b| b.file_score.total_cmp(&a.file_score));
    entries
}

fn format_repo_map(entries: &[RepoMapEntry], token_budget: usize) -> String {
    let char_budget = token_budget * 4; // ~4 chars per token
    let mut output = String::new();
    let mut chars_used = 0;

    for entry in entries {
        // Format file header
        let header = format!("{}\n", entry.path);
        if chars_used + header.len() > char_budget {
            break;
        }
        output.push_str(&header);
        chars_used += header.len();

        // Format each symbol
        for sym in &entry.symbols {
            let line = format!("  {} {} — line {}\n", sym.kind, sym.name, sym.line);
            if chars_used + line.len() > char_budget {
                return output;
            }
            output.push_str(&line);
            chars_used += line.len();
        }

        output.push('\n');
        chars_used += 1;
    }

    output
}

#[cfg(test)]
mod tests {
    use super::*;

    fn sample_files() -> Vec<RepoFile> {
        vec![
            RepoFile {
                path: "src/parser.rs".to_string(),
                content: "pub fn parse() {}".to_string(),
                dependencies: vec!["src/token.rs".to_string()],
            },
            RepoFile {
                path: "src/token.rs".to_string(),
                content: "pub struct Token".to_string(),
                dependencies: vec![],
            },
            RepoFile {
                path: "src/main.rs".to_string(),
                content: "fn main() { parse(); }".to_string(),
                dependencies: vec!["src/parser.rs".to_string()],
            },
        ]
    }

    #[test]
    fn repomap_returns_ranked_files() {
        let ranked = generate_repomap(&sample_files(), "parse token");
        assert_eq!(ranked.len(), 3);
        assert!(ranked[0].score >= ranked[1].score);
    }

    #[test]
    fn select_relevant_files_limits_results() {
        let ranked = generate_repomap(&sample_files(), "parse");
        let top = select_relevant_files(&ranked, 2);
        assert_eq!(top.len(), 2);
    }

    #[test]
    fn score_map_contains_all_selected_files() {
        let ranked = generate_repomap(&sample_files(), "token");
        let map = score_map(&ranked);
        assert_eq!(map.len(), 3);
        assert!(map.contains_key("src/token.rs"));
    }

    #[test]
    fn symbol_repomap_produces_output() {
        let files = vec![
            (
                "src/parser.rs".to_string(),
                "pub struct Parser {}\npub fn parse() {}\n".to_string(),
            ),
            (
                "src/token.rs".to_string(),
                "pub struct Token {}\npub enum TokenKind { Ident, Num }\n".to_string(),
            ),
            (
                "src/main.rs".to_string(),
                "fn main() { let p = Parser::new(); }\n".to_string(),
            ),
        ];
        let output = generate_symbol_repomap(&files, "parse", 1000);
        assert!(!output.is_empty(), "repo map should not be empty");
        assert!(
            output.contains("parser.rs"),
            "should include parser.rs: {output}"
        );
    }

    #[test]
    fn symbol_repomap_respects_token_budget() {
        let files = vec![
            (
                "a.rs".to_string(),
                "pub struct Alpha {}\npub struct Beta {}\npub struct Gamma {}\n".to_string(),
            ),
            (
                "b.rs".to_string(),
                "pub struct Delta {}\npub struct Epsilon {}\n".to_string(),
            ),
        ];
        // Very small budget: should truncate
        let small = generate_symbol_repomap(&files, "", 5);
        let large = generate_symbol_repomap(&files, "", 10000);
        assert!(
            small.len() < large.len(),
            "small budget should produce less output"
        );
    }

    #[test]
    fn symbol_repomap_empty_files() {
        let output = generate_symbol_repomap(&[], "query", 1000);
        assert!(output.is_empty());
    }

    #[test]
    fn symbol_repomap_entries_sorted_by_score() {
        let files = vec![
            (
                "hub.rs".to_string(),
                "pub struct Hub {}\npub fn connect() {}\n".to_string(),
            ),
            (
                "client.rs".to_string(),
                "pub struct Client {}\nfn use_hub() { let h = Hub::new(); }\n".to_string(),
            ),
            (
                "main.rs".to_string(),
                "fn main() { let c = Client::new(); let h = Hub::new(); }\n".to_string(),
            ),
        ];
        let entries = generate_symbol_repomap_entries(&files, "");
        assert!(!entries.is_empty());
        // Entries should be sorted by file_score descending
        for w in entries.windows(2) {
            assert!(
                w[0].file_score >= w[1].file_score,
                "entries should be sorted by score descending"
            );
        }
    }

    #[test]
    fn symbol_repomap_query_boosts_relevant() {
        let files = vec![
            (
                "parser.rs".to_string(),
                "pub struct Parser {}\npub fn parse() {}\n".to_string(),
            ),
            (
                "config.rs".to_string(),
                "pub struct Config {}\npub fn load_config() {}\n".to_string(),
            ),
        ];
        let entries = generate_symbol_repomap_entries(&files, "parse");
        assert!(!entries.is_empty());
        // With "parse" query, parser.rs should rank first
        assert_eq!(
            entries[0].path, "parser.rs",
            "parser.rs should rank first for 'parse' query"
        );
    }
}
