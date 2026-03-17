//! OSV malware scanning — check packages against the OSV vulnerability database.
//!
//! Builds the OSV API URL and provides the interface for vulnerability scanning.
//! No actual HTTP calls are made; the implementation is interface-only with
//! a command parser for extracting package info from install commands.

/// Result of scanning a package against OSV.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum ScanResult {
    /// Package has no known vulnerabilities.
    Safe,
    /// Package has known vulnerabilities with the given advisory IDs.
    Vulnerable(Vec<String>),
    /// Package status could not be determined.
    Unknown,
}

/// The OSV API base URL.
const OSV_API_BASE: &str = "https://api.osv.dev/v1";

/// Build the OSV API URL for querying a package.
pub fn osv_query_url(_ecosystem: &str, _package: &str) -> String {
    format!("{}/query", OSV_API_BASE)
}

/// Build the JSON body for an OSV query request.
pub fn osv_query_body(ecosystem: &str, package: &str) -> serde_json::Value {
    serde_json::json!({
        "package": {
            "name": package,
            "ecosystem": ecosystem
        }
    })
}

/// Scan a package against the OSV database.
///
/// This is currently a stub that returns `Unknown` — a real implementation
/// would make an HTTP POST to the OSV API endpoint.
pub fn scan_package(ecosystem: &str, package: &str) -> ScanResult {
    let _url = osv_query_url(ecosystem, package);
    let _body = osv_query_body(ecosystem, package);

    // Stub: no actual HTTP call. Return Unknown until wired to reqwest.
    ScanResult::Unknown
}

/// Extract the ecosystem and package name from a shell command.
///
/// Supports:
/// - `npx <package>` / `npm install <package>` -> ("npm", package)
/// - `pip install <package>` / `pip3 install <package>` -> ("PyPI", package)
/// - `gem install <package>` -> ("RubyGems", package)
/// - `cargo install <package>` -> ("crates.io", package)
pub fn extract_package_from_command(cmd: &str) -> Option<(String, String)> {
    let parts: Vec<&str> = cmd.split_whitespace().collect();
    if parts.is_empty() {
        return None;
    }

    match parts[0] {
        "npx" if parts.len() >= 2 => {
            let pkg = parts[1].trim_start_matches('@');
            Some(("npm".to_string(), pkg.to_string()))
        }
        "npm" if parts.len() >= 3 && parts[1] == "install" => {
            let pkg = parts[2].trim_start_matches('@');
            Some(("npm".to_string(), pkg.to_string()))
        }
        "pip" | "pip3" if parts.len() >= 3 && parts[1] == "install" => {
            Some(("PyPI".to_string(), parts[2].to_string()))
        }
        "gem" if parts.len() >= 3 && parts[1] == "install" => {
            Some(("RubyGems".to_string(), parts[2].to_string()))
        }
        "cargo" if parts.len() >= 3 && parts[1] == "install" => {
            Some(("crates.io".to_string(), parts[2].to_string()))
        }
        _ => None,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn scan_package_returns_unknown_stub() {
        let result = scan_package("npm", "express");
        assert_eq!(result, ScanResult::Unknown);
    }

    #[test]
    fn extract_npx_command() {
        let result = extract_package_from_command("npx create-react-app");
        assert_eq!(
            result,
            Some(("npm".to_string(), "create-react-app".to_string()))
        );
    }

    #[test]
    fn extract_pip_command() {
        let result = extract_package_from_command("pip install requests");
        assert_eq!(result, Some(("PyPI".to_string(), "requests".to_string())));
    }

    #[test]
    fn extract_unknown_command_returns_none() {
        let result = extract_package_from_command("ls -la");
        assert_eq!(result, None);
    }
}
