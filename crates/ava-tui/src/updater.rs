//! Self-update functionality for AVA CLI.
//!
//! Checks GitHub Releases for newer versions, downloads and replaces the binary.
//! Update checks are cached (once per 24h) to avoid API rate limits.

use serde::{Deserialize, Serialize};
use std::path::PathBuf;

const GITHUB_API_URL: &str = "https://api.github.com/repos/ASF-GROUP/AVA/releases/latest";
const CHECK_INTERVAL_SECS: u64 = 86400; // 24 hours

/// Info about an available update.
#[derive(Debug, Clone)]
pub struct UpdateInfo {
    pub current_version: String,
    pub latest_version: String,
    pub download_url: String,
    pub changelog: String,
    pub published_at: String,
}

/// Cached update check result.
#[derive(Debug, Serialize, Deserialize, Default)]
struct UpdateCheckCache {
    last_check_unix: u64,
    latest_version: Option<String>,
    download_url: Option<String>,
}

/// GitHub release API response (subset).
#[derive(Debug, Deserialize)]
struct GitHubRelease {
    tag_name: String,
    body: Option<String>,
    published_at: Option<String>,
    assets: Vec<GitHubAsset>,
}

#[derive(Debug, Deserialize)]
struct GitHubAsset {
    name: String,
    browser_download_url: String,
}

fn current_version() -> &'static str {
    env!("CARGO_PKG_VERSION")
}

fn cache_path() -> PathBuf {
    dirs::home_dir()
        .unwrap_or_default()
        .join(".ava")
        .join("state")
        .join("update-check.json")
}

fn load_cache() -> UpdateCheckCache {
    std::fs::read_to_string(cache_path())
        .ok()
        .and_then(|s| serde_json::from_str(&s).ok())
        .unwrap_or_default()
}

fn save_cache(cache: &UpdateCheckCache) {
    let path = cache_path();
    if let Some(parent) = path.parent() {
        let _ = std::fs::create_dir_all(parent);
    }
    let _ = std::fs::write(
        &path,
        serde_json::to_string_pretty(cache).unwrap_or_default(),
    );
}

fn should_check() -> bool {
    let cache = load_cache();
    let now = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .unwrap_or_default()
        .as_secs();
    now - cache.last_check_unix > CHECK_INTERVAL_SECS
}

fn target_triple() -> Option<&'static str> {
    match (std::env::consts::OS, std::env::consts::ARCH) {
        ("linux", "x86_64") => Some("x86_64-unknown-linux-gnu"),
        ("linux", "aarch64") => Some("aarch64-unknown-linux-gnu"),
        ("macos", "aarch64") => Some("aarch64-apple-darwin"),
        ("macos", "x86_64") => Some("x86_64-apple-darwin"),
        ("windows", "x86_64") => Some("x86_64-pc-windows-msvc"),
        _ => None,
    }
}

/// Check GitHub for a newer version. Returns None if already up to date.
pub async fn check_for_update() -> color_eyre::Result<Option<UpdateInfo>> {
    let client = reqwest::Client::builder()
        .user_agent("ava-cli")
        .timeout(std::time::Duration::from_secs(10))
        .build()?;

    let release: GitHubRelease = client
        .get(GITHUB_API_URL)
        .header("Accept", "application/vnd.github.v3+json")
        .send()
        .await?
        .json()
        .await?;

    let latest = release.tag_name.trim_start_matches('v').to_string();
    let current = current_version().to_string();

    // Save cache
    let triple = target_triple().unwrap_or("unknown");
    let asset_name = format!("ava-{triple}.tar.gz");
    let download_url = release
        .assets
        .iter()
        .find(|a| a.name == asset_name)
        .map(|a| a.browser_download_url.clone())
        .unwrap_or_default();

    let now = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .unwrap_or_default()
        .as_secs();
    save_cache(&UpdateCheckCache {
        last_check_unix: now,
        latest_version: Some(latest.clone()),
        download_url: Some(download_url.clone()),
    });

    // Compare versions (simple string comparison for semver)
    if latest == current || latest.is_empty() {
        return Ok(None);
    }

    // Parse as semver for proper comparison
    let current_parts: Vec<u32> = current.split('.').filter_map(|s| s.parse().ok()).collect();
    let latest_parts: Vec<u32> = latest.split('.').filter_map(|s| s.parse().ok()).collect();

    if latest_parts <= current_parts {
        return Ok(None);
    }

    Ok(Some(UpdateInfo {
        current_version: current,
        latest_version: latest,
        download_url,
        changelog: release.body.unwrap_or_default(),
        published_at: release.published_at.unwrap_or_default(),
    }))
}

/// Download the update and replace the current binary.
pub async fn download_and_replace(info: &UpdateInfo) -> color_eyre::Result<()> {
    use std::io::Write;

    if info.download_url.is_empty() {
        return Err(color_eyre::eyre::eyre!(
            "No download URL for this platform. Download manually from GitHub."
        ));
    }

    eprintln!("Downloading AVA v{}...", info.latest_version);

    let client = reqwest::Client::builder()
        .user_agent("ava-cli")
        .timeout(std::time::Duration::from_secs(120))
        .build()?;

    let response = client.get(&info.download_url).send().await?;
    if !response.status().is_success() {
        return Err(color_eyre::eyre::eyre!(
            "Download failed: HTTP {}",
            response.status()
        ));
    }

    let bytes = response.bytes().await?;
    eprintln!("Downloaded {} bytes", bytes.len());

    // Extract tarball
    let decoder = flate2::read::GzDecoder::new(&bytes[..]);
    let mut archive = tar::Archive::new(decoder);

    let current_exe = std::env::current_exe()?;
    let temp_path = current_exe.with_extension("new");

    // Find the binary in the archive
    let mut found = false;
    for entry in archive.entries()? {
        let mut entry = entry?;
        let path = entry.path()?.to_path_buf();
        let name = path.file_name().and_then(|n| n.to_str()).unwrap_or("");
        if name == "ava" || name == "ava.exe" {
            let mut file = std::fs::File::create(&temp_path)?;
            std::io::copy(&mut entry, &mut file)?;
            file.flush()?;

            // Make executable on unix
            #[cfg(unix)]
            {
                use std::os::unix::fs::PermissionsExt;
                std::fs::set_permissions(&temp_path, std::fs::Permissions::from_mode(0o755))?;
            }

            found = true;
            break;
        }
    }

    if !found {
        let _ = std::fs::remove_file(&temp_path);
        return Err(color_eyre::eyre::eyre!("Binary not found in archive"));
    }

    // Replace current binary (atomic rename on unix)
    let backup_path = current_exe.with_extension("old");
    let _ = std::fs::remove_file(&backup_path); // remove old backup if exists
    std::fs::rename(&current_exe, &backup_path)?;
    std::fs::rename(&temp_path, &current_exe)?;
    let _ = std::fs::remove_file(&backup_path); // clean up backup

    eprintln!("Updated to AVA v{}", info.latest_version);
    Ok(())
}

/// Run the `ava update` command.
pub async fn run_update_command() -> color_eyre::Result<()> {
    eprintln!("Checking for updates...");

    match check_for_update().await {
        Ok(Some(info)) => {
            eprintln!(
                "AVA v{} available (current: v{})",
                info.latest_version, info.current_version
            );
            if !info.changelog.is_empty() {
                let summary: String = info
                    .changelog
                    .lines()
                    .take(8)
                    .collect::<Vec<_>>()
                    .join("\n");
                eprintln!("\n{summary}\n");
            }
            download_and_replace(&info).await?;
        }
        Ok(None) => {
            eprintln!("AVA v{} is already the latest version.", current_version());
        }
        Err(e) => {
            eprintln!("Failed to check for updates: {e}");
            eprintln!("You can download the latest version from:");
            eprintln!("  https://github.com/ASF-GROUP/AVA/releases/latest");
        }
    }
    Ok(())
}

/// Background check that returns a message if an update is available.
/// Used by the startup check (once per 24h).
pub async fn check_and_notify() -> Option<String> {
    if !should_check() {
        // Check cached version
        let cache = load_cache();
        if let Some(ref latest) = cache.latest_version {
            if latest != current_version() {
                let current_parts: Vec<u32> = current_version()
                    .split('.')
                    .filter_map(|s| s.parse().ok())
                    .collect();
                let latest_parts: Vec<u32> =
                    latest.split('.').filter_map(|s| s.parse().ok()).collect();
                if latest_parts > current_parts {
                    return Some(format!(
                        "Update available: AVA v{latest} (current: v{}). Run `ava update` to upgrade.",
                        current_version()
                    ));
                }
            }
        }
        return None;
    }

    match check_for_update().await {
        Ok(Some(info)) => Some(format!(
            "Update available: AVA v{} (current: v{}). Run `ava update` to upgrade.",
            info.latest_version, info.current_version
        )),
        _ => None,
    }
}
