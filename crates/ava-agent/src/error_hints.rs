//! Smart error recovery hints — pattern-match known error signatures and suggest
//! specific workarounds instead of the generic "try a different approach".

/// A known error pattern with its actionable suggestion.
struct ErrorPattern {
    /// Lowercase substring to match in error content.
    pattern: &'static str,
    /// Actionable workaround suggestion shown to the LLM.
    hint: &'static str,
}

/// Ordered list of error patterns. First match wins, so more specific patterns
/// should come before general ones.
const PATTERNS: &[ErrorPattern] = &[
    // Python sandbox / venv
    ErrorPattern {
        pattern: "externally-managed-environment",
        hint: "Create a virtual environment first: `python -m venv .venv && source .venv/bin/activate`, then retry the install.",
    },
    ErrorPattern {
        pattern: "--break-system-packages",
        hint: "Create a virtual environment first: `python -m venv .venv && source .venv/bin/activate`, then retry the install.",
    },
    ErrorPattern {
        pattern: "modulenotfounderror",
        hint: "Install the missing Python module with `pip install <module>` (use a venv if needed).",
    },
    ErrorPattern {
        pattern: "importerror: no module named",
        hint: "Install the missing Python module with `pip install <module>` (use a venv if needed).",
    },
    // Node.js
    ErrorPattern {
        pattern: "cannot find module",
        hint: "Install the missing npm package with `npm install <package>`.",
    },
    ErrorPattern {
        pattern: "module_not_found",
        hint: "Run `npm install` to install project dependencies.",
    },
    // Rust
    ErrorPattern {
        pattern: "error[e0433]",
        hint: "Add the missing dependency to Cargo.toml with `cargo add <crate>`.",
    },
    ErrorPattern {
        pattern: "unresolved import",
        hint: "Check Cargo.toml dependencies and module paths — the crate may not be added yet.",
    },
    // Command availability
    ErrorPattern {
        pattern: "command not found",
        hint: "The command is not installed. Install it first or use an alternative tool.",
    },
    ErrorPattern {
        pattern: "no such file or directory",
        hint: "The file or directory does not exist. Check the path and create it if needed.",
    },
    // Permissions
    ErrorPattern {
        pattern: "eacces",
        hint: "Permission denied. Use a writable directory like the project root or /tmp.",
    },
    ErrorPattern {
        pattern: "permission denied",
        hint: "Permission denied. Try a different directory or adjust the approach to avoid elevated permissions.",
    },
    // Network (sandbox)
    ErrorPattern {
        pattern: "could not resolve host",
        hint: "Network may be restricted in the sandbox. Try an offline approach or check connectivity.",
    },
    ErrorPattern {
        pattern: "network is unreachable",
        hint: "Network is unavailable. Work with local resources only.",
    },
    ErrorPattern {
        pattern: "connection refused",
        hint: "The target service is not running. Start it first or check the port.",
    },
    // Disk
    ErrorPattern {
        pattern: "enospc",
        hint: "Disk space is full. Clean up temporary files or use a different location.",
    },
    ErrorPattern {
        pattern: "no space left on device",
        hint: "Disk space is full. Clean up temporary files or use a different location.",
    },
];

/// Try to match the error content against known patterns and return an actionable hint.
/// Returns `None` if no pattern matches (caller should fall back to the generic message).
pub fn smart_error_hint(error_content: &str) -> Option<&'static str> {
    let lower = error_content.to_lowercase();
    PATTERNS
        .iter()
        .find(|p| lower.contains(p.pattern))
        .map(|p| p.hint)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn matches_externally_managed_environment() {
        let err = "error: externally-managed-environment\n× This environment is externally managed";
        assert!(smart_error_hint(err).unwrap().contains("venv"));
    }

    #[test]
    fn matches_break_system_packages() {
        let err = "To install packages, use --break-system-packages flag";
        assert!(smart_error_hint(err).unwrap().contains("venv"));
    }

    #[test]
    fn matches_module_not_found_python() {
        let err = "ModuleNotFoundError: No module named 'flask'";
        assert!(smart_error_hint(err).unwrap().contains("pip install"));
    }

    #[test]
    fn matches_import_error_python() {
        let err = "ImportError: No module named 'requests'";
        assert!(smart_error_hint(err).unwrap().contains("pip install"));
    }

    #[test]
    fn matches_node_cannot_find_module() {
        let err = "Error: Cannot find module 'express'";
        assert!(smart_error_hint(err).unwrap().contains("npm install"));
    }

    #[test]
    fn matches_rust_e0433() {
        let err = "error[E0433]: failed to resolve: use of undeclared crate or module `serde`";
        assert!(smart_error_hint(err).unwrap().contains("Cargo.toml"));
    }

    #[test]
    fn matches_command_not_found() {
        let err = "bash: rg: command not found";
        assert!(smart_error_hint(err).unwrap().contains("not installed"));
    }

    #[test]
    fn matches_permission_denied() {
        let err = "error: permission denied (os error 13)";
        assert!(smart_error_hint(err).unwrap().contains("Permission denied"));
    }

    #[test]
    fn matches_network_unreachable() {
        let err = "curl: (7) network is unreachable";
        assert!(smart_error_hint(err).unwrap().contains("local resources"));
    }

    #[test]
    fn matches_no_space() {
        let err = "write error: No space left on device";
        assert!(smart_error_hint(err).unwrap().contains("Disk space"));
    }

    #[test]
    fn no_match_returns_none() {
        let err = "some random error that doesn't match any pattern";
        assert!(smart_error_hint(err).is_none());
    }

    #[test]
    fn case_insensitive_matching() {
        let err = "MODULENOTFOUNDERROR: No module named 'numpy'";
        assert!(smart_error_hint(err).is_some());
    }

    #[test]
    fn multi_line_error_matches() {
        let err = "Running pip install flask...\nTraceback (most recent call last):\n  File ...\nerror: externally-managed-environment\nUse --break-system-packages";
        assert!(smart_error_hint(err).unwrap().contains("venv"));
    }
}
