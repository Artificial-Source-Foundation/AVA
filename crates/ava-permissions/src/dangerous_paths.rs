//! Dangerous path detection for destructive commands.
//!
//! Identifies system-critical paths that should never be the target of recursive
//! deletion or similar destructive operations.

use std::path::{Component, Path, PathBuf};

/// Unix system-critical paths that must never be recursively deleted.
const UNIX_DANGEROUS_PATHS: &[&str] = &[
    "/", "/bin", "/sbin", "/usr", "/lib", "/lib64", "/etc", "/sys", "/proc", "/dev", "/var",
    "/boot", "/home", "/root", "/opt",
];

/// Windows system-critical paths (case-insensitive comparison).
const WINDOWS_DANGEROUS_PATHS: &[&str] = &[
    "C:\\",
    "C:\\Windows",
    "C:\\Program Files",
    "C:\\Program Files (x86)",
    "C:\\Users",
];

/// Check whether a path is a dangerous system-critical location that should never
/// be the target of a recursive removal.
///
/// Returns `true` if the path (after normalization) matches a known critical system path.
/// Handles `.`, `..`, trailing slashes, and multiple consecutive separators.
///
/// # Examples
/// ```
/// use ava_permissions::dangerous_paths::is_dangerous_removal_path;
///
/// assert!(is_dangerous_removal_path("/"));
/// assert!(is_dangerous_removal_path("/usr"));
/// assert!(is_dangerous_removal_path("/usr/"));
/// assert!(is_dangerous_removal_path("/usr/../etc"));
/// assert!(!is_dangerous_removal_path("/home/user/project"));
/// assert!(!is_dangerous_removal_path("./src"));
/// ```
pub fn is_dangerous_removal_path(path: &str) -> bool {
    let normalized = normalize_path_string(path);

    // Check Unix paths
    for &dangerous in UNIX_DANGEROUS_PATHS {
        if normalized == dangerous {
            return true;
        }
    }

    // Check Windows paths (case-insensitive)
    let lower = normalized.to_ascii_lowercase();
    for &dangerous in WINDOWS_DANGEROUS_PATHS {
        if lower == dangerous.to_ascii_lowercase() {
            return true;
        }
    }

    false
}

/// Normalize a path string: resolve `.` and `..`, collapse separators, remove trailing slash.
fn normalize_path_string(path: &str) -> String {
    let trimmed = path.trim();
    if trimmed.is_empty() {
        return String::new();
    }

    let p = Path::new(trimmed);
    let mut out = PathBuf::new();

    for component in p.components() {
        match component {
            Component::CurDir => {}
            Component::ParentDir => {
                out.pop();
            }
            Component::Normal(part) => out.push(part),
            Component::RootDir => out.push(Path::new("/")),
            Component::Prefix(prefix) => out.push(prefix.as_os_str()),
        }
    }

    let s = out.to_string_lossy().to_string();

    // Remove trailing slash (unless it IS the root)
    if s.len() > 1 && s.ends_with('/') {
        s[..s.len() - 1].to_string()
    } else {
        s
    }
}

/// Check if a command contains `rm -rf` or `rm -r` targeting a dangerous path.
///
/// Used by the bash command classifier to block destructive removal of system paths.
/// Returns `true` (blocked) if the command is a dangerous recursive removal.
pub fn is_dangerous_rm_command(command: &str) -> bool {
    let lower = command.to_ascii_lowercase();
    let tokens: Vec<&str> = lower.split_ascii_whitespace().collect();

    if tokens.first().copied() != Some("rm") {
        return false;
    }

    // Check for recursive flag (-r, -R, or combined like -rf, -Rf)
    let has_recursive = tokens.iter().skip(1).any(|token| {
        if *token == "--" {
            return false;
        }
        if let Some(rest) = token.strip_prefix('-') {
            if !rest.is_empty() && !rest.starts_with('-') {
                return rest.contains('r') || rest.contains('R');
            }
        }
        *token == "--recursive"
    });

    if !has_recursive {
        return false;
    }

    // Extract path arguments (non-flag tokens after "rm")
    let mut past_double_dash = false;
    for token in tokens.iter().skip(1) {
        if *token == "--" {
            past_double_dash = true;
            continue;
        }
        if !past_double_dash && token.starts_with('-') {
            continue;
        }
        // Strip quotes
        let stripped = token.trim_matches(|c| c == '"' || c == '\'');
        if is_dangerous_removal_path(stripped) {
            return true;
        }
    }

    false
}

#[cfg(test)]
mod tests {
    use super::*;

    // === is_dangerous_removal_path ===

    #[test]
    fn root_is_dangerous() {
        assert!(is_dangerous_removal_path("/"));
    }

    #[test]
    fn bin_is_dangerous() {
        assert!(is_dangerous_removal_path("/bin"));
    }

    #[test]
    fn sbin_is_dangerous() {
        assert!(is_dangerous_removal_path("/sbin"));
    }

    #[test]
    fn usr_is_dangerous() {
        assert!(is_dangerous_removal_path("/usr"));
    }

    #[test]
    fn lib_is_dangerous() {
        assert!(is_dangerous_removal_path("/lib"));
    }

    #[test]
    fn etc_is_dangerous() {
        assert!(is_dangerous_removal_path("/etc"));
    }

    #[test]
    fn var_is_dangerous() {
        assert!(is_dangerous_removal_path("/var"));
    }

    #[test]
    fn boot_is_dangerous() {
        assert!(is_dangerous_removal_path("/boot"));
    }

    #[test]
    fn home_is_dangerous() {
        assert!(is_dangerous_removal_path("/home"));
    }

    #[test]
    fn root_dir_is_dangerous() {
        assert!(is_dangerous_removal_path("/root"));
    }

    #[test]
    fn opt_is_dangerous() {
        assert!(is_dangerous_removal_path("/opt"));
    }

    #[test]
    fn trailing_slash_normalized() {
        assert!(is_dangerous_removal_path("/usr/"));
    }

    #[test]
    fn dot_dot_normalized() {
        assert!(is_dangerous_removal_path("/usr/../etc"));
    }

    #[test]
    fn dot_normalized() {
        assert!(is_dangerous_removal_path("/usr/."));
    }

    #[test]
    fn home_user_project_not_dangerous() {
        assert!(!is_dangerous_removal_path("/home/user/project"));
    }

    #[test]
    fn relative_src_not_dangerous() {
        assert!(!is_dangerous_removal_path("./src"));
    }

    #[test]
    fn usr_local_not_dangerous() {
        assert!(!is_dangerous_removal_path("/usr/local"));
    }

    #[test]
    fn tmp_not_dangerous() {
        assert!(!is_dangerous_removal_path("/tmp"));
    }

    #[test]
    fn windows_c_root_dangerous() {
        assert!(is_dangerous_removal_path("C:\\"));
    }

    #[test]
    fn windows_program_files_dangerous() {
        assert!(is_dangerous_removal_path("C:\\Program Files"));
    }

    // === is_dangerous_rm_command ===

    #[test]
    fn rm_rf_root_blocked() {
        assert!(is_dangerous_rm_command("rm -rf /"));
    }

    #[test]
    fn rm_rf_usr_blocked() {
        assert!(is_dangerous_rm_command("rm -rf /usr"));
    }

    #[test]
    fn rm_r_etc_blocked() {
        assert!(is_dangerous_rm_command("rm -r /etc"));
    }

    #[test]
    fn rm_rf_src_not_blocked() {
        assert!(!is_dangerous_rm_command("rm -rf ./src"));
    }

    #[test]
    fn rm_rf_home_user_project_not_blocked() {
        assert!(!is_dangerous_rm_command("rm -rf /home/user/project"));
    }

    #[test]
    fn rm_single_file_not_blocked() {
        assert!(!is_dangerous_rm_command("rm /etc/foo.txt"));
    }

    #[test]
    fn rm_rf_quoted_root_blocked() {
        assert!(is_dangerous_rm_command("rm -rf \"/\""));
    }

    #[test]
    fn rm_separate_flags_blocked() {
        assert!(is_dangerous_rm_command("rm -r -f /"));
    }

    #[test]
    fn not_rm_command() {
        assert!(!is_dangerous_rm_command("ls /"));
    }
}
