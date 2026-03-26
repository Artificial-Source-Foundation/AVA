//! Symbol-level dependency graph for PageRank-based repo mapping.
//!
//! Each node is a symbol definition (function, struct, trait, etc.) identified by
//! its fully qualified name (`file_path::symbol_name`). Edges represent references
//! from one symbol's file to another symbol's definition.

use std::collections::HashMap;

use petgraph::graph::{DiGraph, NodeIndex};
use petgraph::Direction;

use crate::symbols::{Symbol, SymbolKind, SymbolRef};

/// A node in the symbol dependency graph.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct SymbolNode {
    /// Fully qualified name: `file_path::symbol_name`.
    pub fqn: String,
    pub kind: SymbolKind,
    pub file_path: String,
    pub name: String,
    pub line: usize,
}

/// The symbol-level dependency graph.
#[derive(Debug, Default)]
pub struct SymbolGraph {
    graph: DiGraph<SymbolNode, ()>,
    indices: HashMap<String, NodeIndex>,
    /// Maps file_path -> list of FQNs defined in that file.
    file_to_symbols: HashMap<String, Vec<String>>,
    /// Maps symbol name -> list of FQNs (for resolving references).
    name_to_fqns: HashMap<String, Vec<String>>,
}

impl SymbolGraph {
    pub fn new() -> Self {
        Self::default()
    }

    /// Add a symbol definition to the graph.
    pub fn add_symbol(&mut self, symbol: &Symbol) {
        let fqn = symbol.fqn();
        if self.indices.contains_key(&fqn) {
            return;
        }

        let node = SymbolNode {
            fqn: fqn.clone(),
            kind: symbol.kind,
            file_path: symbol.file_path.clone(),
            name: symbol.name.clone(),
            line: symbol.line,
        };
        let idx = self.graph.add_node(node);
        self.indices.insert(fqn.clone(), idx);

        self.file_to_symbols
            .entry(symbol.file_path.clone())
            .or_default()
            .push(fqn.clone());

        self.name_to_fqns
            .entry(symbol.name.clone())
            .or_default()
            .push(fqn);
    }

    /// Resolve a reference and add edges from all symbols in the referring file
    /// to the referenced symbol(s).
    ///
    /// Resolution priority:
    /// 1. Symbols defined in the same file (skipped — same-file refs aren't interesting)
    /// 2. All matching symbols in other files
    pub fn resolve_reference(&mut self, reference: &SymbolRef) {
        let Some(target_fqns) = self.name_to_fqns.get(&reference.name).cloned() else {
            return;
        };

        // Find all symbols defined in the referring file
        let source_fqns: Vec<String> = self
            .file_to_symbols
            .get(&reference.file_path)
            .cloned()
            .unwrap_or_default();

        if source_fqns.is_empty() {
            return;
        }

        for target_fqn in &target_fqns {
            // Skip same-file references
            if let Some(node) = self.indices.get(target_fqn) {
                if self.graph[*node].file_path == reference.file_path {
                    continue;
                }
            }

            let Some(&target_idx) = self.indices.get(target_fqn) else {
                continue;
            };

            // Add edge from the first symbol in the source file to the target.
            // Using just the first avoids O(n*m) edge explosion.
            if let Some(source_fqn) = source_fqns.first() {
                if let Some(&source_idx) = self.indices.get(source_fqn) {
                    if self.graph.find_edge(source_idx, target_idx).is_none() {
                        self.graph.add_edge(source_idx, target_idx, ());
                    }
                }
            }
        }
    }

    /// Build a complete symbol graph from extracted definitions and references.
    pub fn build(all_symbols: &[Symbol], all_refs: &[SymbolRef]) -> Self {
        let mut graph = Self::new();

        // Phase 1: add all definitions
        for sym in all_symbols {
            graph.add_symbol(sym);
        }

        // Phase 2: resolve references
        for reference in all_refs {
            graph.resolve_reference(reference);
        }

        graph
    }

    /// All FQNs in the graph.
    pub fn nodes(&self) -> Vec<String> {
        self.indices.keys().cloned().collect()
    }

    /// Get a symbol node by FQN.
    pub fn get(&self, fqn: &str) -> Option<&SymbolNode> {
        self.indices.get(fqn).map(|idx| &self.graph[*idx])
    }

    /// Symbols defined in a given file.
    pub fn symbols_in_file(&self, file_path: &str) -> Vec<&SymbolNode> {
        self.file_to_symbols
            .get(file_path)
            .map(|fqns| fqns.iter().filter_map(|fqn| self.get(fqn)).collect())
            .unwrap_or_default()
    }

    /// Symbols that this symbol references (outgoing edges).
    pub fn outgoing(&self, fqn: &str) -> Vec<String> {
        let Some(&idx) = self.indices.get(fqn) else {
            return Vec::new();
        };
        self.graph
            .neighbors(idx)
            .map(|n| self.graph[n].fqn.clone())
            .collect()
    }

    /// Symbols that reference this symbol (incoming edges).
    pub fn incoming(&self, fqn: &str) -> Vec<String> {
        let Some(&idx) = self.indices.get(fqn) else {
            return Vec::new();
        };
        self.graph
            .neighbors_directed(idx, Direction::Incoming)
            .map(|n| self.graph[n].fqn.clone())
            .collect()
    }

    pub fn node_count(&self) -> usize {
        self.graph.node_count()
    }

    pub fn edge_count(&self) -> usize {
        self.graph.edge_count()
    }

    /// Map of file_path -> list of FQNs.
    pub fn file_symbols(&self) -> &HashMap<String, Vec<String>> {
        &self.file_to_symbols
    }

    /// Aggregate a per-symbol score map into a per-file score map.
    /// Each file's score is the sum of its symbols' scores.
    pub fn aggregate_file_scores(
        &self,
        symbol_scores: &HashMap<String, f64>,
    ) -> HashMap<String, f64> {
        let mut file_scores = HashMap::new();
        for (file_path, fqns) in &self.file_to_symbols {
            let score: f64 = fqns
                .iter()
                .map(|fqn| symbol_scores.get(fqn).copied().unwrap_or(0.0))
                .sum();
            file_scores.insert(file_path.clone(), score);
        }
        file_scores
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::symbols::{Symbol, SymbolKind, SymbolRef};

    fn make_sym(file: &str, name: &str, kind: SymbolKind) -> Symbol {
        Symbol {
            kind,
            name: name.to_string(),
            file_path: file.to_string(),
            line: 1,
        }
    }

    fn make_ref(file: &str, name: &str) -> SymbolRef {
        SymbolRef {
            name: name.to_string(),
            file_path: file.to_string(),
            line: 1,
        }
    }

    #[test]
    fn build_and_query() {
        let symbols = vec![
            make_sym("a.rs", "Parser", SymbolKind::Struct),
            make_sym("a.rs", "parse", SymbolKind::Function),
            make_sym("b.rs", "Token", SymbolKind::Struct),
            make_sym("c.rs", "main", SymbolKind::Function),
        ];
        let refs = vec![
            make_ref("a.rs", "Token"),  // a.rs references Token from b.rs
            make_ref("c.rs", "Parser"), // c.rs references Parser from a.rs
        ];

        let graph = SymbolGraph::build(&symbols, &refs);
        assert_eq!(graph.node_count(), 4);
        assert!(graph.edge_count() >= 2);

        // a.rs -> b.rs (via Token reference)
        let a_out = graph.outgoing("a.rs::Parser");
        assert!(
            a_out.contains(&"b.rs::Token".to_string()),
            "a.rs should reference b.rs::Token: {a_out:?}"
        );

        // c.rs -> a.rs (via Parser reference)
        let c_out = graph.outgoing("c.rs::main");
        assert!(
            c_out.contains(&"a.rs::Parser".to_string()),
            "c.rs should reference a.rs::Parser: {c_out:?}"
        );
    }

    #[test]
    fn symbols_in_file() {
        let symbols = vec![
            make_sym("a.rs", "Foo", SymbolKind::Struct),
            make_sym("a.rs", "Bar", SymbolKind::Struct),
            make_sym("b.rs", "Baz", SymbolKind::Struct),
        ];
        let graph = SymbolGraph::build(&symbols, &[]);
        let a_syms = graph.symbols_in_file("a.rs");
        assert_eq!(a_syms.len(), 2);
    }

    #[test]
    fn no_self_file_edges() {
        let symbols = vec![
            make_sym("a.rs", "Foo", SymbolKind::Struct),
            make_sym("a.rs", "Bar", SymbolKind::Function),
        ];
        // Foo reference from same file should NOT create an edge
        let refs = vec![make_ref("a.rs", "Foo")];
        let graph = SymbolGraph::build(&symbols, &refs);
        assert_eq!(
            graph.edge_count(),
            0,
            "same-file references should not create edges"
        );
    }

    #[test]
    fn aggregate_file_scores_works() {
        let symbols = vec![
            make_sym("a.rs", "Foo", SymbolKind::Struct),
            make_sym("a.rs", "bar", SymbolKind::Function),
            make_sym("b.rs", "Baz", SymbolKind::Struct),
        ];
        let graph = SymbolGraph::build(&symbols, &[]);
        let mut scores = HashMap::new();
        scores.insert("a.rs::Foo".to_string(), 0.3);
        scores.insert("a.rs::bar".to_string(), 0.2);
        scores.insert("b.rs::Baz".to_string(), 0.5);

        let file_scores = graph.aggregate_file_scores(&scores);
        assert!((file_scores["a.rs"] - 0.5).abs() < 1e-10);
        assert!((file_scores["b.rs"] - 0.5).abs() < 1e-10);
    }

    #[test]
    fn incoming_edges() {
        let symbols = vec![
            make_sym("a.rs", "Parser", SymbolKind::Struct),
            make_sym("b.rs", "Runner", SymbolKind::Struct),
            make_sym("c.rs", "Main", SymbolKind::Function),
        ];
        let refs = vec![make_ref("b.rs", "Parser"), make_ref("c.rs", "Parser")];
        let graph = SymbolGraph::build(&symbols, &refs);
        let incoming = graph.incoming("a.rs::Parser");
        assert_eq!(
            incoming.len(),
            2,
            "Parser should have 2 incoming refs: {incoming:?}"
        );
    }

    #[test]
    fn empty_graph() {
        let graph = SymbolGraph::build(&[], &[]);
        assert_eq!(graph.node_count(), 0);
        assert_eq!(graph.edge_count(), 0);
        assert!(graph.symbols_in_file("x.rs").is_empty());
        assert!(graph.outgoing("x.rs::Foo").is_empty());
    }
}
