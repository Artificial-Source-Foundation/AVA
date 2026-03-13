use crate::DependencyGraph;
use std::collections::{HashSet, VecDeque};

#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct ImpactSummary {
    pub changed_files: Vec<String>,
    pub directly_impacted_files: Vec<String>,
    pub transitively_impacted_files: Vec<String>,
    pub likely_test_files: Vec<String>,
}

pub fn analyze_change_impact(
    graph: &DependencyGraph,
    changed_files: &[String],
    max_depth: usize,
) -> ImpactSummary {
    let mut direct = HashSet::new();
    let mut transitive = HashSet::new();

    for changed in changed_files {
        for incoming in graph.incoming(changed) {
            direct.insert(incoming.clone());

            let mut queue = VecDeque::from([(incoming, 1usize)]);
            let mut seen = HashSet::new();
            while let Some((node, depth)) = queue.pop_front() {
                if !seen.insert(node.clone()) {
                    continue;
                }
                transitive.insert(node.clone());
                if depth >= max_depth {
                    continue;
                }
                for next in graph.incoming(&node) {
                    queue.push_back((next, depth + 1));
                }
            }
        }
    }

    for changed in changed_files {
        direct.remove(changed);
        transitive.remove(changed);
    }
    for direct_file in &direct {
        transitive.remove(direct_file);
    }

    let mut changed_sorted = changed_files.to_vec();
    changed_sorted.sort();

    let mut direct_sorted: Vec<String> = direct.into_iter().collect();
    direct_sorted.sort();

    let mut transitive_sorted: Vec<String> = transitive.into_iter().collect();
    transitive_sorted.sort();

    let mut likely_tests: Vec<String> = direct_sorted
        .iter()
        .chain(transitive_sorted.iter())
        .filter(|path| is_test_like_path(path))
        .cloned()
        .collect();
    likely_tests.sort();
    likely_tests.dedup();

    ImpactSummary {
        changed_files: changed_sorted,
        directly_impacted_files: direct_sorted,
        transitively_impacted_files: transitive_sorted,
        likely_test_files: likely_tests,
    }
}

fn is_test_like_path(path: &str) -> bool {
    let lower = path.to_lowercase();
    lower.contains("test") || lower.contains("spec")
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn impact_identifies_direct_and_transitive_dependencies() {
        let mut graph = DependencyGraph::new();
        graph.add_dependency("src/service.rs", "src/model.rs");
        graph.add_dependency("src/api.rs", "src/service.rs");
        graph.add_dependency("tests/service_test.rs", "src/service.rs");

        let changed = vec!["src/model.rs".to_string()];
        let impact = analyze_change_impact(&graph, &changed, 3);

        assert!(impact
            .directly_impacted_files
            .contains(&"src/service.rs".to_string()));
        assert!(impact
            .transitively_impacted_files
            .contains(&"src/api.rs".to_string()));
        assert!(impact
            .likely_test_files
            .contains(&"tests/service_test.rs".to_string()));
    }

    #[test]
    fn impact_respects_depth_limit() {
        let mut graph = DependencyGraph::new();
        graph.add_dependency("a.rs", "b.rs");
        graph.add_dependency("c.rs", "a.rs");
        graph.add_dependency("d.rs", "c.rs");

        let changed = vec!["b.rs".to_string()];
        let impact = analyze_change_impact(&graph, &changed, 2);

        assert!(impact.directly_impacted_files.contains(&"a.rs".to_string()));
        assert!(impact
            .transitively_impacted_files
            .contains(&"c.rs".to_string()));
        assert!(!impact
            .transitively_impacted_files
            .contains(&"d.rs".to_string()));
    }
}
