//! Command canonicalization for approval caching.
//!
//! Normalizes shell commands so that approval decisions persist across
//! equivalent invocations. Inspired by Codex CLI's approach.
//!
//! Handles:
//! - Shell wrapper stripping (`bash -c "cargo test"` → `["cargo", "test"]`)
//! - Env var prefix removal (`RUST_LOG=debug cargo test` → `["cargo", "test"]`)
//! - Path normalization (`/usr/bin/cargo` → `cargo`)
//! - Pipe and chain operator preservation

/// Common binary prefixes to strip from command paths.
const BIN_PREFIXES: &[&str] = &[
    "/usr/local/bin/",
    "/usr/bin/",
    "/bin/",
    "/usr/local/sbin/",
    "/usr/sbin/",
    "/sbin/",
    "/snap/bin/",
    "/opt/homebrew/bin/",
];

/// Shell wrappers that should be unwrapped to reveal the inner command.
const SHELL_WRAPPERS: &[&str] = &["bash", "sh", "zsh", "dash"];

/// Flags on shell wrappers that precede the command string.
const SHELL_CMD_FLAGS: &[&str] = &["-c", "-lc", "-ic", "-lic"];

/// Canonicalize a command string into a normalized token list.
///
/// Transformations applied:
/// 1. Strip shell wrappers (`bash -c "inner cmd"` → tokens of `inner cmd`)
/// 2. Strip env var prefixes (`KEY=val cmd` → tokens of `cmd`)
/// 3. Normalize paths (`/usr/bin/cargo` → `cargo`)
/// 4. Preserve pipe (`|`) and chain (`&&`, `||`, `;`) operators as tokens
pub fn canonicalize_command(command: &str) -> Vec<String> {
    let trimmed = command.trim();
    if trimmed.is_empty() {
        return Vec::new();
    }

    // Tokenize preserving operators
    let raw_tokens = tokenize(trimmed);
    if raw_tokens.is_empty() {
        return Vec::new();
    }

    // Check for shell wrapper and unwrap if found
    let tokens = unwrap_shell(&raw_tokens);

    // Strip env var prefixes and normalize paths
    let mut result = Vec::new();
    let mut past_env_vars = false;

    for (i, token) in tokens.iter().enumerate() {
        // Operators always pass through
        if is_operator(token) {
            past_env_vars = false; // reset for next command segment
            result.push(token.clone());
            continue;
        }

        // Strip leading env var assignments (KEY=value)
        if !past_env_vars && is_env_assignment(token) {
            continue;
        }
        past_env_vars = true;

        // Normalize binary paths on the first word of each command segment
        let is_first_in_segment = i == 0
            || tokens
                .get(i.wrapping_sub(1))
                .is_some_and(|prev| is_operator(prev))
            || (!past_env_vars);

        if is_first_in_segment || is_command_position(&result) {
            result.push(normalize_bin_path(token));
        } else {
            result.push(token.clone());
        }
    }

    result
}

/// Produce a deterministic string key from a command's canonical form.
/// Used for approval cache lookups.
pub fn canonical_key(command: &str) -> String {
    canonicalize_command(command).join(" ")
}

/// Check whether two command strings have the same canonical form.
pub fn commands_equivalent(a: &str, b: &str) -> bool {
    canonicalize_command(a) == canonicalize_command(b)
}

/// Tokenize a command string, preserving shell operators as distinct tokens.
/// Handles single and double quoting.
fn tokenize(input: &str) -> Vec<String> {
    let mut tokens = Vec::new();
    let mut current = String::new();
    let mut chars = input.chars().peekable();
    let mut in_single_quote = false;
    let mut in_double_quote = false;

    while let Some(ch) = chars.next() {
        match ch {
            '\\' if !in_single_quote => {
                // Escaped character — take the next char literally
                if let Some(next) = chars.next() {
                    current.push(next);
                }
            }
            '\'' if !in_double_quote => {
                in_single_quote = !in_single_quote;
                // Don't push the quote itself
            }
            '"' if !in_single_quote => {
                in_double_quote = !in_double_quote;
                // Don't push the quote itself
            }
            '|' if !in_single_quote && !in_double_quote => {
                flush_token(&mut current, &mut tokens);
                if chars.peek() == Some(&'|') {
                    chars.next();
                    tokens.push("||".to_string());
                } else {
                    tokens.push("|".to_string());
                }
            }
            '&' if !in_single_quote && !in_double_quote => {
                flush_token(&mut current, &mut tokens);
                if chars.peek() == Some(&'&') {
                    chars.next();
                    tokens.push("&&".to_string());
                }
                // Single & (background) — skip it, not relevant for canonicalization
            }
            ';' if !in_single_quote && !in_double_quote => {
                flush_token(&mut current, &mut tokens);
                tokens.push(";".to_string());
            }
            ' ' | '\t' if !in_single_quote && !in_double_quote => {
                flush_token(&mut current, &mut tokens);
            }
            _ => {
                current.push(ch);
            }
        }
    }
    flush_token(&mut current, &mut tokens);
    tokens
}

fn flush_token(current: &mut String, tokens: &mut Vec<String>) {
    if !current.is_empty() {
        tokens.push(std::mem::take(current));
    }
}

/// Check if a token is a shell operator.
fn is_operator(token: &str) -> bool {
    matches!(token, "|" | "&&" | "||" | ";")
}

/// Check if a token looks like an env var assignment (KEY=value, no spaces).
fn is_env_assignment(token: &str) -> bool {
    if let Some(eq_pos) = token.find('=') {
        if eq_pos == 0 {
            return false;
        }
        let key = &token[..eq_pos];
        // Env var names: uppercase/lowercase letters, digits, underscore; must start with letter or _
        let first = key.as_bytes()[0];
        if !(first.is_ascii_alphabetic() || first == b'_') {
            return false;
        }
        key.bytes().all(|b| b.is_ascii_alphanumeric() || b == b'_')
    } else {
        false
    }
}

/// Strip common binary path prefixes from a command name.
fn normalize_bin_path(token: &str) -> String {
    for prefix in BIN_PREFIXES {
        if let Some(stripped) = token.strip_prefix(prefix) {
            if !stripped.is_empty() && !stripped.contains('/') {
                return stripped.to_string();
            }
        }
    }
    token.to_string()
}

/// Check if the last non-operator token in result indicates we're at a command position.
/// (i.e., the result is empty or the last token is an operator)
fn is_command_position(result: &[String]) -> bool {
    result.is_empty() || result.last().is_some_and(|t| is_operator(t))
}

/// Attempt to unwrap shell wrappers like `bash -c "inner command"`.
/// Returns the inner command's tokens if a wrapper is detected.
fn unwrap_shell(tokens: &[String]) -> Vec<String> {
    if tokens.len() < 3 {
        return tokens.to_vec();
    }

    let cmd_name = normalize_bin_path(&tokens[0]);
    if !SHELL_WRAPPERS.contains(&cmd_name.as_str()) {
        return tokens.to_vec();
    }

    // Look for -c, -lc, etc. flag
    let flag_pos = tokens[1..]
        .iter()
        .position(|t| SHELL_CMD_FLAGS.contains(&t.as_str()));

    if let Some(pos) = flag_pos {
        let flag_idx = pos + 1; // adjust for the slice offset
                                // The command string follows the flag
        if flag_idx + 1 < tokens.len() {
            // Everything after the flag is the inner command — rejoin and re-tokenize
            let inner = tokens[flag_idx + 1..].join(" ");
            let inner_tokens = tokenize(&inner);
            if !inner_tokens.is_empty() {
                // Recursively unwrap in case of nested wrappers
                return unwrap_shell(&inner_tokens);
            }
        }
    }

    tokens.to_vec()
}

#[cfg(test)]
mod tests {
    use super::*;

    // === Shell wrapper stripping ===

    #[test]
    fn strip_bash_c_wrapper() {
        assert_eq!(
            canonicalize_command(r#"bash -c "cargo test""#),
            vec!["cargo", "test"]
        );
    }

    #[test]
    fn strip_sh_c_wrapper() {
        assert_eq!(canonicalize_command(r#"sh -c "ls -la""#), vec!["ls", "-la"]);
    }

    #[test]
    fn strip_bash_lc_wrapper() {
        assert_eq!(
            canonicalize_command(r#"bash -lc "npm run build""#),
            vec!["npm", "run", "build"]
        );
    }

    #[test]
    fn strip_zsh_c_wrapper() {
        assert_eq!(
            canonicalize_command(r#"zsh -c "make all""#),
            vec!["make", "all"]
        );
    }

    #[test]
    fn nested_shell_wrapper() {
        assert_eq!(
            canonicalize_command(r#"bash -c "sh -c 'cargo test'""#),
            vec!["cargo", "test"]
        );
    }

    // === Env var prefix removal ===

    #[test]
    fn strip_single_env_var() {
        assert_eq!(
            canonicalize_command("RUST_LOG=debug cargo test"),
            vec!["cargo", "test"]
        );
    }

    #[test]
    fn strip_multiple_env_vars() {
        assert_eq!(
            canonicalize_command("CC=gcc CXX=g++ CFLAGS=-O2 make all"),
            vec!["make", "all"]
        );
    }

    #[test]
    fn env_var_with_path_value() {
        assert_eq!(
            canonicalize_command("PATH=/usr/bin:$PATH cargo build"),
            vec!["cargo", "build"]
        );
    }

    #[test]
    fn env_var_after_operator_stripped() {
        assert_eq!(
            canonicalize_command("cd dir && RUST_LOG=debug cargo test"),
            vec!["cd", "dir", "&&", "cargo", "test"]
        );
    }

    // === Path normalization ===

    #[test]
    fn normalize_usr_bin_path() {
        assert_eq!(
            canonicalize_command("/usr/bin/cargo test"),
            vec!["cargo", "test"]
        );
    }

    #[test]
    fn normalize_usr_local_bin_path() {
        assert_eq!(
            canonicalize_command("/usr/local/bin/node app.js"),
            vec!["node", "app.js"]
        );
    }

    #[test]
    fn normalize_bin_path() {
        assert_eq!(canonicalize_command("/bin/ls -la"), vec!["ls", "-la"]);
    }

    #[test]
    fn normalize_snap_bin_path() {
        assert_eq!(
            canonicalize_command("/snap/bin/go build ./..."),
            vec!["go", "build", "./..."]
        );
    }

    #[test]
    fn normalize_homebrew_path() {
        assert_eq!(
            canonicalize_command("/opt/homebrew/bin/python3 script.py"),
            vec!["python3", "script.py"]
        );
    }

    #[test]
    fn unknown_path_preserved() {
        // Paths we don't recognize should be kept
        assert_eq!(
            canonicalize_command("/home/user/.cargo/bin/custom-tool --flag"),
            vec!["/home/user/.cargo/bin/custom-tool", "--flag"]
        );
    }

    // === Pipe and chain preservation ===

    #[test]
    fn pipe_preserved() {
        assert_eq!(
            canonicalize_command("cargo test | head"),
            vec!["cargo", "test", "|", "head"]
        );
    }

    #[test]
    fn chain_and_preserved() {
        assert_eq!(
            canonicalize_command("cd dir && cargo test"),
            vec!["cd", "dir", "&&", "cargo", "test"]
        );
    }

    #[test]
    fn chain_or_preserved() {
        assert_eq!(
            canonicalize_command("cargo test || echo failed"),
            vec!["cargo", "test", "||", "echo", "failed"]
        );
    }

    #[test]
    fn semicolon_preserved() {
        assert_eq!(
            canonicalize_command("echo hello; echo world"),
            vec!["echo", "hello", ";", "echo", "world"]
        );
    }

    #[test]
    fn mixed_operators() {
        assert_eq!(
            canonicalize_command("cd dir && cargo test | head; echo done"),
            vec!["cd", "dir", "&&", "cargo", "test", "|", "head", ";", "echo", "done"]
        );
    }

    // === Equivalence checks ===

    #[test]
    fn equivalent_with_shell_wrapper() {
        assert!(commands_equivalent("cargo test", r#"bash -c "cargo test""#));
    }

    #[test]
    fn equivalent_with_env_vars() {
        assert!(commands_equivalent(
            "cargo test",
            "RUST_LOG=debug cargo test"
        ));
    }

    #[test]
    fn equivalent_with_full_path() {
        assert!(commands_equivalent("cargo test", "/usr/bin/cargo test"));
    }

    #[test]
    fn equivalent_combined_normalization() {
        // All three normalizations at once
        assert!(commands_equivalent(
            "cargo test",
            r#"bash -c "RUST_LOG=debug /usr/bin/cargo test""#
        ));
    }

    #[test]
    fn not_equivalent_different_commands() {
        assert!(!commands_equivalent("cargo test", "cargo build"));
    }

    #[test]
    fn not_equivalent_different_args() {
        assert!(!commands_equivalent(
            "cargo test --workspace",
            "cargo test -p ava-permissions"
        ));
    }

    // === canonical_key ===

    #[test]
    fn canonical_key_deterministic() {
        let k1 = canonical_key("cargo test");
        let k2 = canonical_key(r#"bash -c "cargo test""#);
        assert_eq!(k1, k2);
        assert_eq!(k1, "cargo test");
    }

    #[test]
    fn canonical_key_with_pipe() {
        assert_eq!(
            canonical_key("cargo test | head -5"),
            "cargo test | head -5"
        );
    }

    // === Edge cases ===

    #[test]
    fn empty_command() {
        assert_eq!(canonicalize_command(""), Vec::<String>::new());
    }

    #[test]
    fn whitespace_only() {
        assert_eq!(canonicalize_command("   "), Vec::<String>::new());
    }

    #[test]
    fn single_command() {
        assert_eq!(canonicalize_command("ls"), vec!["ls"]);
    }

    #[test]
    fn single_command_with_whitespace() {
        assert_eq!(canonicalize_command("  ls  "), vec!["ls"]);
    }

    #[test]
    fn quoted_args_preserved() {
        assert_eq!(
            canonicalize_command(r#"echo "hello world""#),
            vec!["echo", "hello world"]
        );
    }

    #[test]
    fn single_quoted_args_preserved() {
        assert_eq!(
            canonicalize_command("echo 'hello world'"),
            vec!["echo", "hello world"]
        );
    }

    #[test]
    fn shell_wrapper_without_flag_not_stripped() {
        // `bash script.sh` is not a wrapper — it's running a script
        assert_eq!(
            canonicalize_command("bash script.sh"),
            vec!["bash", "script.sh"]
        );
    }

    #[test]
    fn env_var_like_arg_not_stripped() {
        // After the command name, KEY=val is an argument, not env assignment
        assert_eq!(
            canonicalize_command("grep KEY=val file.txt"),
            vec!["grep", "KEY=val", "file.txt"]
        );
    }

    #[test]
    fn path_normalization_in_chained_commands() {
        assert_eq!(
            canonicalize_command("/usr/bin/ls && /bin/cat file.txt"),
            vec!["ls", "&&", "cat", "file.txt"]
        );
    }
}
