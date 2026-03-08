use tree_sitter::{Node, Parser};

/// Extract command words using tree-sitter.
pub(super) fn extract_words_treesitter(command: &str) -> Option<Vec<String>> {
    let mut parser = Parser::new();
    let language = tree_sitter_bash::language();
    parser.set_language(&language).ok()?;
    let tree = parser.parse(command, None)?;
    if tree.root_node().has_error() {
        return None;
    }

    let mut words = Vec::new();
    collect_command_words(tree.root_node(), command.as_bytes(), &mut words);
    if words.is_empty() {
        return None;
    }
    Some(words)
}

fn collect_command_words(node: Node<'_>, source: &[u8], out: &mut Vec<String>) {
    let kind = node.kind();
    if kind == "command_name" || kind == "word" {
        if let Ok(text) = node.utf8_text(source) {
            let cleaned = text.trim_matches(|c: char| c == '"' || c == '\'');
            if !cleaned.is_empty() {
                out.push(cleaned.to_string());
            }
        }
    }

    let mut cursor = node.walk();
    for child in node.children(&mut cursor) {
        collect_command_words(child, source, out);
    }
}

/// Heuristic word extraction fallback.
pub(super) fn extract_words_heuristic(command: &str) -> Vec<String> {
    command
        .split_whitespace()
        .map(|w| w.to_ascii_lowercase())
        .collect()
}
