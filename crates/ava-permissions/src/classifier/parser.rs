use tree_sitter::{Node, Parser};

/// A single command extracted from the AST (no pipes/chains).
#[derive(Debug, Clone)]
pub(super) struct ParsedCommand {
    pub text: String,
    pub words: Vec<String>,
}

/// Parse a bash command string into individual commands using tree-sitter AST.
///
/// Handles pipes (`|`), chains (`&&`, `||`, `;`), subshells (`$(...)`, backticks),
/// and process substitution (`<(...)`, `>(...)`). Returns individual commands
/// with their extracted words.
///
/// Falls back to heuristic splitting on parse failure, but marks the result
/// so callers can apply stricter scrutiny.
pub(super) fn parse_command_ast(command: &str) -> AstParseResult {
    let mut parser = Parser::new();
    let language = tree_sitter_bash::language();
    if parser.set_language(&language).is_err() {
        return AstParseResult::Fallback(split_heuristic(command));
    }

    let Some(tree) = parser.parse(command, None) else {
        return AstParseResult::Fallback(split_heuristic(command));
    };

    // If the tree has errors, we can still extract commands from valid parts,
    // but flag it so callers can be stricter.
    let has_errors = tree.root_node().has_error();

    let mut commands = Vec::new();
    collect_commands(tree.root_node(), command.as_bytes(), &mut commands);

    if commands.is_empty() {
        // Tree parsed but no commands found — try whole string as single command
        let words = extract_words_from_node(tree.root_node(), command.as_bytes());
        if !words.is_empty() {
            commands.push(ParsedCommand {
                text: command.to_string(),
                words,
            });
        }
    }

    if commands.is_empty() {
        return AstParseResult::Fallback(split_heuristic(command));
    }

    if has_errors {
        AstParseResult::PartialAst(commands)
    } else {
        AstParseResult::FullAst(commands)
    }
}

/// Result of AST parsing with quality indicator.
#[derive(Debug)]
pub(super) enum AstParseResult {
    /// Full AST parse succeeded — high confidence in command boundaries.
    FullAst(Vec<ParsedCommand>),
    /// AST had errors but we extracted what we could — apply extra scrutiny.
    PartialAst(Vec<ParsedCommand>),
    /// Tree-sitter failed entirely — fell back to heuristic splitting.
    Fallback(Vec<ParsedCommand>),
}

impl AstParseResult {
    pub fn commands(&self) -> &[ParsedCommand] {
        match self {
            Self::FullAst(cmds) | Self::PartialAst(cmds) | Self::Fallback(cmds) => cmds,
        }
    }

    /// Whether this parse result should trigger extra scrutiny
    /// (partial parse or heuristic fallback).
    pub fn needs_extra_scrutiny(&self) -> bool {
        !matches!(self, Self::FullAst(_))
    }
}

/// Recursively collect individual commands from the AST.
///
/// Recognizes:
/// - `command` — simple command (`ls -la`)
/// - `pipeline` — piped commands (`cat foo | grep bar`)
/// - `list` — chained commands (`cmd1 && cmd2`)
/// - `subshell` — `(cmd)` or `$(cmd)`
/// - `command_substitution` — `$(cmd)` or `` `cmd` ``
/// - `process_substitution` — `<(cmd)` or `>(cmd)`
fn collect_commands(node: Node<'_>, source: &[u8], out: &mut Vec<ParsedCommand>) {
    let kind = node.kind();

    match kind {
        "command" => {
            let text = node.utf8_text(source).unwrap_or("").to_string();
            let words = extract_words_from_node(node, source);
            if !words.is_empty() {
                out.push(ParsedCommand { text, words });
            }
        }
        // For pipelines, collect each command in the pipeline separately
        "pipeline" => {
            let mut cursor = node.walk();
            for child in node.children(&mut cursor) {
                if child.kind() == "command" || child.kind() == "redirected_statement" {
                    let text = child.utf8_text(source).unwrap_or("").to_string();
                    let words = extract_words_from_node(child, source);
                    if !words.is_empty() {
                        out.push(ParsedCommand { text, words });
                    }
                } else {
                    collect_commands(child, source, out);
                }
            }
        }
        // For lists (&&, ||, ;), recurse into both sides
        "list" => {
            let mut cursor = node.walk();
            for child in node.children(&mut cursor) {
                collect_commands(child, source, out);
            }
        }
        // Subshells, command substitution, process substitution — recurse
        "subshell" | "command_substitution" | "process_substitution" => {
            let mut cursor = node.walk();
            for child in node.children(&mut cursor) {
                collect_commands(child, source, out);
            }
        }
        // Redirected statement wraps a command with redirections.
        // IMPORTANT: Use the FULL redirected_statement text (including redirect targets
        // like `>> /etc/passwd`) so pattern matching can detect dangerous redirects.
        "redirected_statement" => {
            let full_text = node.utf8_text(source).unwrap_or("").to_string();
            let words = extract_words_from_node(node, source);
            if !words.is_empty() {
                out.push(ParsedCommand {
                    text: full_text,
                    words,
                });
            }
        }
        // Program root — recurse into children
        "program" => {
            let mut cursor = node.walk();
            for child in node.children(&mut cursor) {
                collect_commands(child, source, out);
            }
        }
        // For other node types (if_statement, for_statement, etc.), recurse
        _ => {
            let mut cursor = node.walk();
            for child in node.children(&mut cursor) {
                collect_commands(child, source, out);
            }
        }
    }
}

/// Extract words from a command node and its descendants.
fn extract_words_from_node(node: Node<'_>, source: &[u8]) -> Vec<String> {
    let mut words = Vec::new();
    collect_words_recursive(node, source, &mut words);
    words
}

fn collect_words_recursive(node: Node<'_>, source: &[u8], out: &mut Vec<String>) {
    let kind = node.kind();

    match kind {
        "command_name" | "word" | "raw_string" | "string_content" => {
            if let Ok(text) = node.utf8_text(source) {
                let cleaned = text.trim_matches(|c: char| c == '"' || c == '\'');
                if !cleaned.is_empty() {
                    out.push(cleaned.to_string());
                }
            }
        }
        // For string nodes, extract the content without quotes
        "string" | "concatenation" => {
            if let Ok(text) = node.utf8_text(source) {
                let cleaned = text.trim_matches(|c: char| c == '"' || c == '\'');
                if !cleaned.is_empty() {
                    out.push(cleaned.to_string());
                }
            }
        }
        // Skip operators, redirections, etc.
        "|" | "&&" | "||" | ";" | ">" | ">>" | "<" | "2>" | "&>" | "(" | ")" => {}
        // For command_substitution inside arguments, extract the inner command
        "command_substitution" | "process_substitution" => {
            let mut cursor = node.walk();
            for child in node.children(&mut cursor) {
                collect_words_recursive(child, source, out);
            }
        }
        // Recurse for everything else
        _ => {
            let mut cursor = node.walk();
            for child in node.children(&mut cursor) {
                collect_words_recursive(child, source, out);
            }
        }
    }
}

/// Heuristic fallback: split on pipes and chains, extract words.
fn split_heuristic(command: &str) -> Vec<ParsedCommand> {
    let mut parts = Vec::new();
    let mut current = String::new();
    let mut chars = command.chars().peekable();
    let mut in_single_quote = false;
    let mut in_double_quote = false;
    let mut prev_char = None;

    while let Some(ch) = chars.next() {
        match ch {
            '\'' if !in_double_quote && prev_char != Some('\\') => {
                in_single_quote = !in_single_quote;
                current.push(ch);
            }
            '"' if !in_single_quote && prev_char != Some('\\') => {
                in_double_quote = !in_double_quote;
                current.push(ch);
            }
            '|' if !in_single_quote && !in_double_quote => {
                if chars.peek() == Some(&'|') {
                    chars.next();
                }
                if !current.trim().is_empty() {
                    let text = current.trim().to_string();
                    let words = extract_words_heuristic(&text);
                    parts.push(ParsedCommand { text, words });
                }
                current.clear();
            }
            '&' if !in_single_quote && !in_double_quote => {
                if chars.peek() == Some(&'&') {
                    chars.next();
                }
                if !current.trim().is_empty() {
                    let text = current.trim().to_string();
                    let words = extract_words_heuristic(&text);
                    parts.push(ParsedCommand { text, words });
                }
                current.clear();
            }
            ';' if !in_single_quote && !in_double_quote => {
                if !current.trim().is_empty() {
                    let text = current.trim().to_string();
                    let words = extract_words_heuristic(&text);
                    parts.push(ParsedCommand { text, words });
                }
                current.clear();
            }
            _ => {
                current.push(ch);
            }
        }
        prev_char = Some(ch);
    }

    if !current.trim().is_empty() {
        let text = current.trim().to_string();
        let words = extract_words_heuristic(&text);
        parts.push(ParsedCommand { text, words });
    }
    parts
}

/// Extract command words using tree-sitter (legacy API, kept for compatibility).
pub(super) fn extract_words_treesitter(command: &str) -> Option<Vec<String>> {
    let mut parser = Parser::new();
    let language = tree_sitter_bash::language();
    parser.set_language(&language).ok()?;
    let tree = parser.parse(command, None)?;
    if tree.root_node().has_error() {
        return None;
    }

    let words = extract_words_from_node(tree.root_node(), command.as_bytes());
    if words.is_empty() {
        return None;
    }
    Some(words)
}

/// Heuristic word extraction fallback.
pub(super) fn extract_words_heuristic(command: &str) -> Vec<String> {
    command
        .split_whitespace()
        .map(|w| w.to_ascii_lowercase())
        .collect()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn ast_parses_simple_command() {
        let result = parse_command_ast("ls -la");
        assert!(matches!(result, AstParseResult::FullAst(_)));
        let cmds = result.commands();
        assert_eq!(cmds.len(), 1);
        assert!(cmds[0].words.iter().any(|w| w == "ls"));
    }

    #[test]
    fn ast_splits_pipeline() {
        let result = parse_command_ast("cat file.txt | grep foo | wc -l");
        let cmds = result.commands();
        assert!(
            cmds.len() >= 3,
            "should split pipeline into 3+ commands, got {}",
            cmds.len()
        );
    }

    #[test]
    fn ast_splits_chain() {
        let result = parse_command_ast("make && make test");
        let cmds = result.commands();
        assert!(
            cmds.len() >= 2,
            "should split chain into 2+ commands, got {}",
            cmds.len()
        );
    }

    #[test]
    fn ast_handles_command_substitution() {
        let result = parse_command_ast("echo $(whoami)");
        let cmds = result.commands();
        // Should find both `echo` and `whoami`
        let all_words: Vec<&str> = cmds
            .iter()
            .flat_map(|c| c.words.iter().map(|w| w.as_str()))
            .collect();
        assert!(
            all_words.iter().any(|w| *w == "echo"),
            "should find echo: {all_words:?}"
        );
        assert!(
            all_words.iter().any(|w| *w == "whoami"),
            "should find whoami: {all_words:?}"
        );
    }

    #[test]
    fn ast_handles_quoted_strings() {
        let result = parse_command_ast(r#"echo "hello world""#);
        let cmds = result.commands();
        assert!(!cmds.is_empty());
        assert!(cmds[0].words.iter().any(|w| w == "echo"));
    }

    #[test]
    fn ast_handles_semicolons() {
        let result = parse_command_ast("cd /tmp; ls; pwd");
        let cmds = result.commands();
        assert!(
            cmds.len() >= 3,
            "should split on semicolons, got {}",
            cmds.len()
        );
    }

    #[test]
    fn ast_fallback_on_invalid() {
        // Intentionally malformed — should fall back to heuristic
        let result = parse_command_ast("<<<<<<< HEAD");
        assert!(result.needs_extra_scrutiny());
    }

    #[test]
    fn ast_detects_subshell_commands() {
        let result = parse_command_ast("rm -rf $(echo /)");
        let cmds = result.commands();
        let all_words: Vec<&str> = cmds
            .iter()
            .flat_map(|c| c.words.iter().map(|w| w.as_str()))
            .collect();
        // Should find both `rm` and `echo` as separate commands
        assert!(
            all_words.iter().any(|w| *w == "rm"),
            "should find rm: {all_words:?}"
        );
        assert!(
            all_words.iter().any(|w| *w == "echo"),
            "should find echo: {all_words:?}"
        );
    }

    #[test]
    fn heuristic_fallback_splits_correctly() {
        let cmds = split_heuristic("echo hello | grep h && ls");
        assert_eq!(cmds.len(), 3);
    }
}
