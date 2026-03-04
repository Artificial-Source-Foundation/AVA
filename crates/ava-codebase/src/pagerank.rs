use std::collections::HashMap;

use crate::graph::DependencyGraph;

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
}
