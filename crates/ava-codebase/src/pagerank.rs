use std::collections::HashMap;

use crate::graph::DependencyGraph;
use crate::symbol_graph::SymbolGraph;

pub fn calculate_pagerank(
    graph: &DependencyGraph,
    damping: f64,
    iterations: usize,
) -> HashMap<String, f64> {
    let nodes = graph.nodes();
    let n = nodes.len();
    if n == 0 {
        return HashMap::new();
    }

    let base = (1.0 - damping) / n as f64;
    let mut rank: HashMap<String, f64> = nodes
        .iter()
        .map(|node| (node.clone(), 1.0 / n as f64))
        .collect();

    for _ in 0..iterations {
        let mut next: HashMap<String, f64> = nodes.iter().map(|n| (n.clone(), base)).collect();
        for node in &nodes {
            let outgoing = graph.outgoing(node);
            if outgoing.is_empty() {
                continue;
            }
            let share = damping * rank[node] / outgoing.len() as f64;
            for dep in outgoing {
                if let Some(v) = next.get_mut(&dep) {
                    *v += share;
                }
            }
        }
        rank = next;
    }

    rank
}

pub fn extract_keywords(query: &str) -> Vec<String> {
    query
        .split(|c: char| !c.is_alphanumeric() && c != '_' && c != '-')
        .map(str::trim)
        .filter(|s| !s.is_empty())
        .map(|s| s.to_lowercase())
        .collect()
}

pub fn calculate_relevance(
    path: &str,
    content: &str,
    keywords: &[String],
    pagerank: &HashMap<String, f64>,
) -> f64 {
    let base = *pagerank.get(path).unwrap_or(&0.0);
    if keywords.is_empty() {
        return base;
    }

    let haystack = format!("{}\n{}", path.to_lowercase(), content.to_lowercase());
    let keyword_hits = keywords
        .iter()
        .filter(|k| haystack.contains(k.as_str()))
        .count() as f64;
    base + keyword_hits * 0.25
}

/// Run PageRank on a symbol-level graph.
///
/// Same algorithm as `calculate_pagerank` but operates on `SymbolGraph` nodes/edges.
pub fn calculate_symbol_pagerank(
    graph: &SymbolGraph,
    damping: f64,
    iterations: usize,
) -> HashMap<String, f64> {
    let nodes = graph.nodes();
    let n = nodes.len();
    if n == 0 {
        return HashMap::new();
    }

    let base = (1.0 - damping) / n as f64;
    let mut rank: HashMap<String, f64> = nodes
        .iter()
        .map(|node| (node.clone(), 1.0 / n as f64))
        .collect();

    for _ in 0..iterations {
        let mut next: HashMap<String, f64> = nodes.iter().map(|n| (n.clone(), base)).collect();
        for node in &nodes {
            let outgoing = graph.outgoing(node);
            if outgoing.is_empty() {
                continue;
            }
            let share = damping * rank[node] / outgoing.len() as f64;
            for dep in outgoing {
                if let Some(v) = next.get_mut(&dep) {
                    *v += share;
                }
            }
        }
        rank = next;
    }

    rank
}

/// Personalized PageRank biased toward seed nodes.
///
/// Instead of uniform teleportation, the random surfer teleports back to `seed_nodes`.
/// This gives higher scores to symbols structurally close to the seeds (query-relevant symbols).
pub fn personalized_pagerank(
    graph: &SymbolGraph,
    seed_nodes: &[String],
    damping: f64,
    iterations: usize,
) -> HashMap<String, f64> {
    let nodes = graph.nodes();
    let n = nodes.len();
    if n == 0 || seed_nodes.is_empty() {
        return calculate_symbol_pagerank(graph, damping, iterations);
    }

    // Teleportation distribution: concentrated on seed nodes
    let seed_weight = 1.0 / seed_nodes.len() as f64;
    let teleport: HashMap<String, f64> = nodes
        .iter()
        .map(|node| {
            let weight = if seed_nodes.contains(node) {
                seed_weight
            } else {
                0.0
            };
            (node.clone(), weight)
        })
        .collect();

    let mut rank: HashMap<String, f64> = nodes
        .iter()
        .map(|node| (node.clone(), 1.0 / n as f64))
        .collect();

    for _ in 0..iterations {
        let mut next: HashMap<String, f64> = nodes
            .iter()
            .map(|n| {
                (
                    n.clone(),
                    (1.0 - damping) * teleport.get(n).copied().unwrap_or(0.0),
                )
            })
            .collect();
        for node in &nodes {
            let outgoing = graph.outgoing(node);
            if outgoing.is_empty() {
                continue;
            }
            let share = damping * rank[node] / outgoing.len() as f64;
            for dep in outgoing {
                if let Some(v) = next.get_mut(&dep) {
                    *v += share;
                }
            }
        }
        rank = next;
    }

    rank
}

/// Find seed FQNs in the symbol graph that match query keywords.
pub fn find_seed_nodes(graph: &SymbolGraph, query: &str) -> Vec<String> {
    let keywords = extract_keywords(query);
    if keywords.is_empty() {
        return Vec::new();
    }

    graph
        .nodes()
        .into_iter()
        .filter(|fqn| {
            let name = fqn.rsplit("::").next().unwrap_or(fqn).to_lowercase();
            keywords.iter().any(|kw| name.contains(kw.as_str()))
        })
        .collect()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn pagerank_returns_scores_for_all_nodes() {
        let mut graph = DependencyGraph::new();
        graph.add_dependency("a.rs", "b.rs");
        graph.add_dependency("b.rs", "c.rs");

        let scores = calculate_pagerank(&graph, 0.85, 20);
        assert_eq!(scores.len(), 3);
        assert!(scores.values().all(|v| *v >= 0.0));
    }

    #[test]
    fn extract_keywords_keeps_word_tokens() {
        let k = extract_keywords("find Parser::parse in src");
        assert!(k.contains(&"find".to_string()));
        assert!(k.contains(&"parse".to_string()));
        assert!(k.contains(&"src".to_string()));
    }

    #[test]
    fn relevance_boosts_keyword_matches() {
        let mut pr = HashMap::new();
        pr.insert("src/a.rs".to_string(), 0.1);
        let keys = vec!["parser".to_string()];
        let rel = calculate_relevance("src/a.rs", "struct Parser;", &keys, &pr);
        assert!(rel > 0.1);
    }

    #[test]
    fn symbol_pagerank_converges() {
        use crate::symbols::{Symbol, SymbolKind, SymbolRef};

        let symbols = vec![
            Symbol {
                kind: SymbolKind::Struct,
                name: "Parser".into(),
                file_path: "a.rs".into(),
                line: 1,
            },
            Symbol {
                kind: SymbolKind::Struct,
                name: "Token".into(),
                file_path: "b.rs".into(),
                line: 1,
            },
            Symbol {
                kind: SymbolKind::Function,
                name: "main".into(),
                file_path: "c.rs".into(),
                line: 1,
            },
        ];
        let refs = vec![
            SymbolRef {
                name: "Token".into(),
                file_path: "a.rs".into(),
                line: 5,
            },
            SymbolRef {
                name: "Parser".into(),
                file_path: "c.rs".into(),
                line: 2,
            },
        ];
        let graph = crate::symbol_graph::SymbolGraph::build(&symbols, &refs);
        let scores = calculate_symbol_pagerank(&graph, 0.85, 20);

        assert_eq!(scores.len(), 3);
        assert!(scores.values().all(|v| *v >= 0.0));
        // Token is referenced by Parser, Parser is referenced by main
        // Both should have higher rank than main (which is only referenced by nobody)
        let token_score = scores["b.rs::Token"];
        let main_score = scores["c.rs::main"];
        assert!(
            token_score > main_score,
            "Token ({token_score}) should rank higher than main ({main_score})"
        );
    }

    #[test]
    fn personalized_pagerank_boosts_seeds() {
        use crate::symbols::{Symbol, SymbolKind, SymbolRef};

        let symbols = vec![
            Symbol {
                kind: SymbolKind::Struct,
                name: "Alpha".into(),
                file_path: "a.rs".into(),
                line: 1,
            },
            Symbol {
                kind: SymbolKind::Struct,
                name: "Beta".into(),
                file_path: "b.rs".into(),
                line: 1,
            },
            Symbol {
                kind: SymbolKind::Struct,
                name: "Gamma".into(),
                file_path: "c.rs".into(),
                line: 1,
            },
        ];
        // Alpha -> Beta, Beta -> Gamma (linear chain)
        let refs = vec![
            SymbolRef {
                name: "Beta".into(),
                file_path: "a.rs".into(),
                line: 2,
            },
            SymbolRef {
                name: "Gamma".into(),
                file_path: "b.rs".into(),
                line: 2,
            },
        ];
        let graph = crate::symbol_graph::SymbolGraph::build(&symbols, &refs);

        // Standard PageRank: Gamma should score highest (end of chain, most incoming)
        let standard = calculate_symbol_pagerank(&graph, 0.85, 20);

        // Personalized to Alpha: Alpha should get boosted
        let personalized = personalized_pagerank(&graph, &["a.rs::Alpha".to_string()], 0.85, 20);

        assert!(
            personalized["a.rs::Alpha"] > standard["a.rs::Alpha"],
            "personalized Alpha ({}) should be higher than standard Alpha ({})",
            personalized["a.rs::Alpha"],
            standard["a.rs::Alpha"]
        );
    }

    #[test]
    fn find_seed_nodes_matches_keywords() {
        use crate::symbols::{Symbol, SymbolKind};

        let symbols = vec![
            Symbol {
                kind: SymbolKind::Struct,
                name: "Parser".into(),
                file_path: "a.rs".into(),
                line: 1,
            },
            Symbol {
                kind: SymbolKind::Function,
                name: "parse_input".into(),
                file_path: "b.rs".into(),
                line: 1,
            },
            Symbol {
                kind: SymbolKind::Struct,
                name: "Config".into(),
                file_path: "c.rs".into(),
                line: 1,
            },
        ];
        let graph = crate::symbol_graph::SymbolGraph::build(&symbols, &[]);

        let seeds = find_seed_nodes(&graph, "parse tokens");
        assert!(seeds.contains(&"a.rs::Parser".to_string()));
        assert!(seeds.contains(&"b.rs::parse_input".to_string()));
        assert!(!seeds.contains(&"c.rs::Config".to_string()));
    }

    #[test]
    fn empty_graph_pagerank() {
        let graph = crate::symbol_graph::SymbolGraph::new();
        let scores = calculate_symbol_pagerank(&graph, 0.85, 20);
        assert!(scores.is_empty());
    }

    #[test]
    fn personalized_empty_seeds_falls_back() {
        use crate::symbols::{Symbol, SymbolKind};

        let symbols = vec![Symbol {
            kind: SymbolKind::Struct,
            name: "Foo".into(),
            file_path: "a.rs".into(),
            line: 1,
        }];
        let graph = crate::symbol_graph::SymbolGraph::build(&symbols, &[]);
        let scores = personalized_pagerank(&graph, &[], 0.85, 20);
        assert_eq!(scores.len(), 1);
    }
}
