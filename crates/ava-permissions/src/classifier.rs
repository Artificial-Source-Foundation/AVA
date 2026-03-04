use std::collections::HashSet;

use tree_sitter::{Node, Parser};

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum RiskLevel {
    Low,
    Medium,
    High,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct CommandRisk {
    pub destructive: bool,
    pub network: bool,
    pub risk: RiskLevel,
}

pub fn classify_bash_command(command: &str) -> CommandRisk {
    let mut parser = Parser::new();
    let language = tree_sitter_bash::language();
    if parser.set_language(&language).is_err() {
        return heuristic_classify(command);
    }

    let Some(tree) = parser.parse(command, None) else {
        return heuristic_classify(command);
    };
    if tree.root_node().has_error() {
        return heuristic_classify(command);
    }

    let mut words = Vec::new();
    collect_command_words(tree.root_node(), command.as_bytes(), &mut words);
    if words.is_empty() {
        return heuristic_classify(command);
    }

    let words_set: HashSet<String> = words.into_iter().map(|w| w.to_ascii_lowercase()).collect();
    let destructive_cmds = ["rm", "mkfs", "dd", "shutdown", "reboot", "chown"];
    let network_cmds = ["curl", "wget", "nc", "ping", "ssh", "scp", "ftp"];

    let destructive = destructive_cmds.iter().any(|cmd| words_set.contains(*cmd))
        || command.contains("rm -rf")
        || command.contains("rm -fr")
        || command.contains(":(){");
    let network = network_cmds.iter().any(|cmd| words_set.contains(*cmd))
        || command.contains("http://")
        || command.contains("https://");

    let risk = if destructive {
        RiskLevel::High
    } else if network {
        RiskLevel::Medium
    } else {
        RiskLevel::Low
    };

    CommandRisk {
        destructive,
        network,
        risk,
    }
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

fn heuristic_classify(command: &str) -> CommandRisk {
    let lower = command.to_ascii_lowercase();
    let destructive = ["rm -rf", "rm -fr", "mkfs", "dd if=", "shutdown", "reboot"]
        .iter()
        .any(|n| lower.contains(n));
    let network = ["curl ", "wget ", "http://", "https://", "ssh "]
        .iter()
        .any(|n| lower.contains(n));

    let risk = if destructive {
        RiskLevel::High
    } else if network {
        RiskLevel::Medium
    } else {
        RiskLevel::Low
    };

    CommandRisk {
        destructive,
        network,
        risk,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn marks_destructive_command_high_risk() {
        let risk = classify_bash_command("rm -rf /tmp/test");
        assert!(risk.destructive);
        assert_eq!(risk.risk, RiskLevel::High);
    }

    #[test]
    fn marks_network_command_medium_risk() {
        let risk = classify_bash_command("curl https://example.com");
        assert!(risk.network);
        assert_eq!(risk.risk, RiskLevel::Medium);
    }

    #[test]
    fn marks_safe_command_low_risk() {
        let risk = classify_bash_command("ls -la src");
        assert!(!risk.destructive);
        assert!(!risk.network);
        assert_eq!(risk.risk, RiskLevel::Low);
    }
}
