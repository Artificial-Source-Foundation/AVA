use std::path::{Component, Path, PathBuf};

use crate::tags::RiskLevel;

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct PathRisk {
    pub risk_level: RiskLevel,
    pub outside_workspace: bool,
    pub system_path: bool,
    pub reason: Option<String>,
}

const SYSTEM_PREFIXES: &[&str] = &[
    "/etc", "/usr", "/bin", "/sbin", "/lib", "/lib64",
    "/boot", "/sys", "/proc", "/var/run",
];

/// Analyze the risk of accessing a file path relative to the workspace.
pub fn analyze_path(path: &str, workspace_root: &Path) -> PathRisk {
    let normalized = normalize_path(Path::new(path), workspace_root);
    let path_str = normalized.to_string_lossy();

    // Root path
    if path_str == "/" {
        return PathRisk {
            risk_level: RiskLevel::Critical,
            outside_workspace: true,
            system_path: true,
            reason: Some("Operating on root filesystem".to_string()),
        };
    }

    // System paths
    for prefix in SYSTEM_PREFIXES {
        if path_str.starts_with(prefix) {
            return PathRisk {
                risk_level: RiskLevel::Critical,
                outside_workspace: true,
                system_path: true,
                reason: Some(format!("System path: {prefix}")),
            };
        }
    }

    let ws = normalize_path(workspace_root, workspace_root);

    // Inside workspace
    if normalized.starts_with(&ws) {
        return PathRisk {
            risk_level: RiskLevel::Safe,
            outside_workspace: false,
            system_path: false,
            reason: None,
        };
    }

    // /tmp is low risk
    if path_str.starts_with("/tmp") {
        return PathRisk {
            risk_level: RiskLevel::Low,
            outside_workspace: true,
            system_path: false,
            reason: Some("Temporary directory".to_string()),
        };
    }

    // Home directory (outside workspace)
    if let Some(home) = home_dir() {
        if normalized.starts_with(&home) {
            return PathRisk {
                risk_level: RiskLevel::Medium,
                outside_workspace: true,
                system_path: false,
                reason: Some("Home directory (outside workspace)".to_string()),
            };
        }
    }

    // Other paths outside workspace
    PathRisk {
        risk_level: RiskLevel::High,
        outside_workspace: true,
        system_path: false,
        reason: Some("Outside workspace".to_string()),
    }
}

fn home_dir() -> Option<PathBuf> {
    std::env::var("HOME").ok().map(PathBuf::from)
}

fn normalize_path(path: &Path, base: &Path) -> PathBuf {
    let abs_path = if path.is_absolute() {
        path.to_path_buf()
    } else {
        base.join(path)
    };

    let mut out = PathBuf::new();
    for part in abs_path.components() {
        match part {
            Component::CurDir => {}
            Component::ParentDir => {
                out.pop();
            }
            Component::Normal(part) => out.push(part),
            Component::RootDir => out.push("/"),
            Component::Prefix(prefix) => out.push(prefix.as_os_str()),
        }
    }
    out
}

#[cfg(test)]
mod tests {
    use super::*;

    fn ws() -> PathBuf {
        PathBuf::from("/home/user/project")
    }

    #[test]
    fn inside_workspace_is_safe() {
        let result = analyze_path("/home/user/project/src/main.rs", &ws());
        assert_eq!(result.risk_level, RiskLevel::Safe);
        assert!(!result.outside_workspace);
        assert!(!result.system_path);
    }

    #[test]
    fn relative_path_inside_workspace() {
        let result = analyze_path("src/main.rs", &ws());
        assert_eq!(result.risk_level, RiskLevel::Safe);
        assert!(!result.outside_workspace);
    }

    #[test]
    fn tmp_is_low_risk() {
        let result = analyze_path("/tmp/test.txt", &ws());
        assert_eq!(result.risk_level, RiskLevel::Low);
        assert!(result.outside_workspace);
    }

    #[test]
    fn system_paths_are_critical() {
        for path in &["/etc/passwd", "/usr/bin/ls", "/bin/sh", "/boot/vmlinuz", "/proc/cpuinfo", "/sys/class"] {
            let result = analyze_path(path, &ws());
            assert_eq!(result.risk_level, RiskLevel::Critical, "Expected Critical for {path}");
            assert!(result.system_path, "Expected system_path for {path}");
        }
    }

    #[test]
    fn root_is_critical() {
        let result = analyze_path("/", &ws());
        assert_eq!(result.risk_level, RiskLevel::Critical);
        assert!(result.system_path);
    }

    #[test]
    fn outside_workspace_is_high() {
        let result = analyze_path("/opt/something/file.txt", &ws());
        assert_eq!(result.risk_level, RiskLevel::High);
        assert!(result.outside_workspace);
    }

    #[test]
    fn parent_traversal_outside_workspace() {
        let result = analyze_path("../../../etc/passwd", &ws());
        assert_eq!(result.risk_level, RiskLevel::Critical);
        assert!(result.system_path);
    }
}
