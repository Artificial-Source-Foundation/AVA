use std::collections::HashMap;

use petgraph::graph::{DiGraph, NodeIndex};

#[derive(Debug, Default)]
pub struct DependencyGraph {
    graph: DiGraph<String, ()>,
    indices: HashMap<String, NodeIndex>,
}

impl DependencyGraph {
    pub fn new() -> Self {
        Self::default()
    }

    pub fn add_file(&mut self, path: impl Into<String>) {
        let path = path.into();
        if self.indices.contains_key(&path) {
            return;
        }
        let idx = self.graph.add_node(path.clone());
        self.indices.insert(path, idx);
    }

    pub fn add_dependency(&mut self, from: impl Into<String>, to: impl Into<String>) {
        let from = from.into();
        let to = to.into();
        self.add_file(from.clone());
        self.add_file(to.clone());

        let from_idx = self.indices[&from];
        let to_idx = self.indices[&to];
        if self.graph.find_edge(from_idx, to_idx).is_none() {
            self.graph.add_edge(from_idx, to_idx, ());
        }
    }

    pub fn nodes(&self) -> Vec<String> {
        self.indices.keys().cloned().collect()
    }

    pub fn outgoing(&self, from: &str) -> Vec<String> {
        let Some(&idx) = self.indices.get(from) else {
            return Vec::new();
        };
        self.graph
            .neighbors(idx)
            .map(|n| self.graph[n].clone())
            .collect()
    }

    pub fn incoming(&self, to: &str) -> Vec<String> {
        let Some(&target) = self.indices.get(to) else {
            return Vec::new();
        };
        self.graph
            .node_indices()
            .filter(|&n| self.graph.find_edge(n, target).is_some())
            .map(|n| self.graph[n].clone())
            .collect()
    }

    pub fn out_degree(&self, node: &str) -> usize {
        self.outgoing(node).len()
    }

    pub fn in_degree(&self, node: &str) -> usize {
        self.incoming(node).len()
    }

    pub fn node_count(&self) -> usize {
        self.graph.node_count()
    }

    pub fn edge_count(&self) -> usize {
        self.graph.edge_count()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn add_nodes_and_edges() {
        let mut g = DependencyGraph::new();
        g.add_dependency("a.rs", "b.rs");
        g.add_dependency("a.rs", "c.rs");
        assert_eq!(g.node_count(), 3);
        assert_eq!(g.edge_count(), 2);
    }

    #[test]
    fn no_duplicate_edges() {
        let mut g = DependencyGraph::new();
        g.add_dependency("a.rs", "b.rs");
        g.add_dependency("a.rs", "b.rs");
        assert_eq!(g.edge_count(), 1);
    }

    #[test]
    fn incoming_outgoing_work() {
        let mut g = DependencyGraph::new();
        g.add_dependency("a.rs", "b.rs");
        g.add_dependency("c.rs", "b.rs");
        assert_eq!(g.out_degree("a.rs"), 1);
        assert_eq!(g.in_degree("b.rs"), 2);
    }
}
