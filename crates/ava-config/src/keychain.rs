//! OS keychain credential storage with encrypted file fallback.
//!
//! Stores API keys in the OS-native secret store (macOS Keychain, Linux Secret Service,
//! Windows Credential Manager) via the `keyring` crate. When the OS keychain is unavailable
//! (e.g., headless Linux without D-Bus), falls back to AES-256-GCM encrypted JSON with
//! PBKDF2-derived key from a master password.

use std::path::{Path, PathBuf};

use ava_types::{AvaError, Result};
use serde::{Deserialize, Serialize};
use tracing::{debug, info, warn};

use crate::credentials::{CredentialStore, ProviderCredential};

/// Service name used for OS keychain entries.
const KEYCHAIN_SERVICE: &str = "ava-cli";

/// Username for the provider index entry in the OS keychain.
const INDEX_USERNAME: &str = "__ava_provider_index";

/// File name for the encrypted credential store.
const ENCRYPTED_FILE: &str = "credentials.enc";

/// PBKDF2 iteration count for key derivation.
const PBKDF2_ITERATIONS: u32 = 600_000;

/// AES-256-GCM nonce size in bytes.
const NONCE_SIZE: usize = 12;

/// Salt size in bytes for PBKDF2.
const SALT_SIZE: usize = 32;

// ── Encrypted file format ────────────────────────────────────────────────

/// On-disk format for the encrypted credential store.
#[derive(Debug, Serialize, Deserialize)]
struct EncryptedEnvelope {
    /// Base64-encoded salt for PBKDF2.
    salt: String,
    /// Base64-encoded AES-256-GCM nonce.
    nonce: String,
    /// Base64-encoded ciphertext (encrypted JSON of CredentialStore).
    ciphertext: String,
}

// ── KeychainManager ──────────────────────────────────────────────────────

/// Manages credential storage with OS keychain primary and encrypted file fallback.
pub struct KeychainManager {
    /// Path to the encrypted fallback file (typically ~/.ava/credentials.enc).
    encrypted_path: PathBuf,
    /// Whether the OS keychain is available (detected at construction).
    os_keychain_available: bool,
}

impl KeychainManager {
    /// Create a new KeychainManager, probing OS keychain availability.
    pub fn new() -> Result<Self> {
        let home = dirs::home_dir().ok_or_else(|| {
            AvaError::ConfigError("Could not resolve home directory".to_string())
        })?;
        let encrypted_path = home.join(".ava").join(ENCRYPTED_FILE);
        let os_keychain_available = probe_os_keychain();

        if os_keychain_available {
            debug!("OS keychain available — using native secret storage");
        } else {
            debug!("OS keychain unavailable — will use encrypted file fallback");
        }

        Ok(Self {
            encrypted_path,
            os_keychain_available,
        })
    }

    /// Create with an explicit encrypted file path (for testing).
    #[cfg(test)]
    pub fn with_path(encrypted_path: PathBuf) -> Self {
        Self {
            encrypted_path,
            os_keychain_available: false, // tests always use encrypted file
        }
    }

    /// Store a credential for a provider.
    pub fn store(&self, provider: &str, credential: &ProviderCredential) -> Result<()> {
        let json = serde_json::to_string(credential)
            .map_err(|e| AvaError::SerializationError(e.to_string()))?;

        if self.os_keychain_available {
            match store_in_os_keychain(provider, &json) {
                Ok(()) => {
                    // Update the provider index
                    let _ = update_provider_index(provider, false);
                    return Ok(());
                }
                Err(e) => {
                    warn!("OS keychain store failed, falling back to encrypted file: {e}");
                }
            }
        }

        // Fallback: store in encrypted file
        let mut store = self.load_encrypted_store()?;
        store.set(provider, credential.clone());
        self.save_encrypted_store(&store)
    }

    /// Retrieve a credential for a provider.
    pub fn retrieve(&self, provider: &str) -> Result<Option<ProviderCredential>> {
        if self.os_keychain_available {
            match retrieve_from_os_keychain(provider) {
                Ok(Some(json)) => {
                    let cred: ProviderCredential = serde_json::from_str(&json)
                        .map_err(|e| AvaError::SerializationError(e.to_string()))?;
                    return Ok(Some(cred));
                }
                Ok(None) => {}
                Err(e) => {
                    warn!("OS keychain retrieve failed, falling back to encrypted file: {e}");
                }
            }
        }

        // Fallback: read from encrypted file
        let store = self.load_encrypted_store()?;
        Ok(store.providers.get(provider).cloned())
    }

    /// Delete a credential for a provider.
    pub fn delete(&self, provider: &str) -> Result<bool> {
        let mut removed = false;

        if self.os_keychain_available {
            match delete_from_os_keychain(provider) {
                Ok(true) => {
                    let _ = update_provider_index(provider, true);
                    removed = true;
                }
                Ok(false) => {}
                Err(e) => {
                    warn!("OS keychain delete failed: {e}");
                }
            }
        }

        // Also remove from encrypted file if it exists
        if self.encrypted_path.exists() {
            let mut store = self.load_encrypted_store()?;
            if store.remove(provider) {
                self.save_encrypted_store(&store)?;
                removed = true;
            }
        }

        Ok(removed)
    }

    /// List all provider names that have stored credentials.
    pub fn list_providers(&self) -> Result<Vec<String>> {
        let mut providers = Vec::new();

        if self.os_keychain_available {
            if let Ok(index) = load_provider_index() {
                providers.extend(index);
            }
        }

        // Merge with encrypted file providers
        if self.encrypted_path.exists() {
            let store = self.load_encrypted_store()?;
            for key in store.providers.keys() {
                if !providers.contains(key) {
                    providers.push(key.clone());
                }
            }
        }

        providers.sort();
        Ok(providers)
    }

    /// Load all credentials into a CredentialStore.
    pub fn load_all(&self) -> Result<CredentialStore> {
        let providers = self.list_providers()?;
        let mut store = CredentialStore::default();

        for provider in &providers {
            if let Ok(Some(cred)) = self.retrieve(provider) {
                store.set(provider, cred);
            }
        }

        Ok(store)
    }

    /// Store an entire CredentialStore.
    pub fn store_all(&self, store: &CredentialStore) -> Result<()> {
        for (provider, credential) in &store.providers {
            self.store(provider, credential)?;
        }
        Ok(())
    }

    /// Migrate from plaintext credentials.json to secure storage.
    ///
    /// Reads the existing plaintext file, stores each credential in the keychain,
    /// and removes the plaintext file on success.
    pub async fn migrate_from_plaintext(&self, plaintext_path: &Path) -> Result<MigrationResult> {
        if !plaintext_path.exists() {
            return Ok(MigrationResult::NoFileFound);
        }

        let store = CredentialStore::load(plaintext_path).await?;
        if store.providers.is_empty() {
            return Ok(MigrationResult::NoFileFound);
        }

        let count = store.providers.len();
        info!(
            "Migrating {count} provider credentials from plaintext to secure storage"
        );

        // Store each credential
        for (provider, credential) in &store.providers {
            self.store(provider, credential)?;
        }

        // Verify migration by reading back
        for (provider, original) in &store.providers {
            let retrieved = self.retrieve(provider)?;
            match retrieved {
                Some(ref cred) if cred.api_key == original.api_key => {}
                _ => {
                    return Err(AvaError::ConfigError(format!(
                        "Migration verification failed for provider '{provider}' — \
                         plaintext file preserved"
                    )));
                }
            }
        }

        // Remove plaintext file
        tokio::fs::remove_file(plaintext_path)
            .await
            .map_err(|e| AvaError::IoError(format!("Failed to remove plaintext credentials: {e}")))?;

        info!("Migration complete — removed plaintext credentials file");
        Ok(MigrationResult::Migrated { count })
    }

    /// Check if migration is needed (plaintext file exists but secure storage is empty).
    pub fn needs_migration(&self) -> bool {
        let plaintext_path = dirs::home_dir()
            .map(|h| h.join(".ava").join("credentials.json"))
            .unwrap_or_default();
        plaintext_path.exists()
    }

    // ── Encrypted file helpers ───────────────────────────────────────────

    fn load_encrypted_store(&self) -> Result<CredentialStore> {
        if !self.encrypted_path.exists() {
            return Ok(CredentialStore::default());
        }

        let content = std::fs::read_to_string(&self.encrypted_path)
            .map_err(|e| AvaError::IoError(e.to_string()))?;

        let envelope: EncryptedEnvelope = serde_json::from_str(&content)
            .map_err(|e| AvaError::SerializationError(e.to_string()))?;

        let password = get_master_password(false)?;
        decrypt_store(&envelope, &password)
    }

    fn save_encrypted_store(&self, store: &CredentialStore) -> Result<()> {
        if let Some(parent) = self.encrypted_path.parent() {
            std::fs::create_dir_all(parent)
                .map_err(|e| AvaError::IoError(e.to_string()))?;
        }

        let password = get_master_password(true)?;
        let envelope = encrypt_store(store, &password)?;

        let content = serde_json::to_string_pretty(&envelope)
            .map_err(|e| AvaError::SerializationError(e.to_string()))?;

        std::fs::write(&self.encrypted_path, &content)
            .map_err(|e| AvaError::IoError(e.to_string()))?;

        #[cfg(unix)]
        {
            use std::os::unix::fs::PermissionsExt;
            let perms = std::fs::Permissions::from_mode(0o600);
            std::fs::set_permissions(&self.encrypted_path, perms)
                .map_err(|e| AvaError::IoError(e.to_string()))?;
        }

        Ok(())
    }
}

// ── Migration result ─────────────────────────────────────────────────────

/// Result of a plaintext-to-secure migration attempt.
#[derive(Debug, PartialEq, Eq)]
pub enum MigrationResult {
    /// No plaintext credentials file found — nothing to migrate.
    NoFileFound,
    /// Successfully migrated N provider credentials.
    Migrated { count: usize },
}

// ── OS keychain functions ────────────────────────────────────────────────

/// Probe whether the OS keychain is functional by attempting a no-op.
fn probe_os_keychain() -> bool {
    #[cfg(feature = "keychain")]
    {
        // Try to get a non-existent entry — if keyring itself errors (no backend),
        // we know the keychain is unavailable.
        match keyring::Entry::new(KEYCHAIN_SERVICE, "__ava_probe__") {
            Ok(entry) => {
                // Attempt to read — NotFound is fine (keychain works), other errors mean unavailable
                match entry.get_password() {
                    Ok(_) => true,
                    Err(keyring::Error::NoEntry) => true,
                    Err(_) => false,
                }
            }
            Err(_) => false,
        }
    }
    #[cfg(not(feature = "keychain"))]
    {
        false
    }
}

#[cfg(feature = "keychain")]
fn store_in_os_keychain(provider: &str, json: &str) -> std::result::Result<(), String> {
    let entry = keyring::Entry::new(KEYCHAIN_SERVICE, provider)
        .map_err(|e| format!("keyring entry creation failed: {e}"))?;
    entry
        .set_password(json)
        .map_err(|e| format!("keyring set_password failed: {e}"))
}

#[cfg(not(feature = "keychain"))]
fn store_in_os_keychain(_provider: &str, _json: &str) -> std::result::Result<(), String> {
    Err("keychain feature not enabled".to_string())
}

#[cfg(feature = "keychain")]
fn retrieve_from_os_keychain(provider: &str) -> std::result::Result<Option<String>, String> {
    let entry = keyring::Entry::new(KEYCHAIN_SERVICE, provider)
        .map_err(|e| format!("keyring entry creation failed: {e}"))?;
    match entry.get_password() {
        Ok(password) => Ok(Some(password)),
        Err(keyring::Error::NoEntry) => Ok(None),
        Err(e) => Err(format!("keyring get_password failed: {e}")),
    }
}

#[cfg(not(feature = "keychain"))]
fn retrieve_from_os_keychain(_provider: &str) -> std::result::Result<Option<String>, String> {
    Err("keychain feature not enabled".to_string())
}

#[cfg(feature = "keychain")]
fn delete_from_os_keychain(provider: &str) -> std::result::Result<bool, String> {
    let entry = keyring::Entry::new(KEYCHAIN_SERVICE, provider)
        .map_err(|e| format!("keyring entry creation failed: {e}"))?;
    match entry.delete_credential() {
        Ok(()) => Ok(true),
        Err(keyring::Error::NoEntry) => Ok(false),
        Err(e) => Err(format!("keyring delete failed: {e}")),
    }
}

#[cfg(not(feature = "keychain"))]
fn delete_from_os_keychain(_provider: &str) -> std::result::Result<bool, String> {
    Err("keychain feature not enabled".to_string())
}

/// Load the provider index from the OS keychain.
/// The index is stored as a comma-separated list of provider names.
#[cfg(feature = "keychain")]
fn load_provider_index() -> std::result::Result<Vec<String>, String> {
    let entry = keyring::Entry::new(KEYCHAIN_SERVICE, INDEX_USERNAME)
        .map_err(|e| format!("keyring index entry failed: {e}"))?;
    match entry.get_password() {
        Ok(csv) => Ok(csv
            .split(',')
            .filter(|s| !s.is_empty())
            .map(String::from)
            .collect()),
        Err(keyring::Error::NoEntry) => Ok(Vec::new()),
        Err(e) => Err(format!("keyring index read failed: {e}")),
    }
}

#[cfg(not(feature = "keychain"))]
fn load_provider_index() -> std::result::Result<Vec<String>, String> {
    Ok(Vec::new())
}

/// Update the provider index: add or remove a provider name.
#[cfg(feature = "keychain")]
fn update_provider_index(provider: &str, remove: bool) -> std::result::Result<(), String> {
    let mut providers = load_provider_index().unwrap_or_default();

    if remove {
        providers.retain(|p| p != provider);
    } else if !providers.contains(&provider.to_string()) {
        providers.push(provider.to_string());
    }

    let entry = keyring::Entry::new(KEYCHAIN_SERVICE, INDEX_USERNAME)
        .map_err(|e| format!("keyring index entry failed: {e}"))?;

    if providers.is_empty() {
        let _ = entry.delete_credential();
    } else {
        providers.sort();
        entry
            .set_password(&providers.join(","))
            .map_err(|e| format!("keyring index write failed: {e}"))?;
    }

    Ok(())
}

#[cfg(not(feature = "keychain"))]
fn update_provider_index(_provider: &str, _remove: bool) -> std::result::Result<(), String> {
    Ok(())
}

// ── Encryption helpers ───────────────────────────────────────────────────

/// Get the master password for encrypted file backend.
///
/// Reads from AVA_MASTER_PASSWORD env var first, then prompts interactively.
/// The `creating` flag controls the prompt text.
fn get_master_password(creating: bool) -> Result<String> {
    // Check environment variable first (for CI, headless, tests)
    if let Ok(password) = std::env::var("AVA_MASTER_PASSWORD") {
        if !password.is_empty() {
            return Ok(password);
        }
    }

    // Interactive prompt
    #[cfg(feature = "keychain")]
    {
        let prompt = if creating {
            "Enter master password for AVA credential encryption: "
        } else {
            "Enter master password to decrypt AVA credentials: "
        };

        rpassword::prompt_password(prompt)
            .map_err(|e| AvaError::IoError(format!("Failed to read master password: {e}")))
    }

    #[cfg(not(feature = "keychain"))]
    {
        let _ = creating;
        Err(AvaError::ConfigError(
            "Encrypted credential storage requires the 'keychain' feature".to_string(),
        ))
    }
}

/// Encrypt a CredentialStore into an EncryptedEnvelope.
#[cfg(feature = "keychain")]
fn encrypt_store(store: &CredentialStore, password: &str) -> Result<EncryptedEnvelope> {
    use aes_gcm::{aead::Aead, Aes256Gcm, KeyInit, Nonce};
    use base64::Engine;
    use pbkdf2::pbkdf2_hmac;
    use rand::RngCore;
    use sha2::Sha256;

    let plaintext = serde_json::to_string(store)
        .map_err(|e| AvaError::SerializationError(e.to_string()))?;

    // Generate random salt and nonce
    let mut salt = [0u8; SALT_SIZE];
    let mut nonce_bytes = [0u8; NONCE_SIZE];
    rand::thread_rng().fill_bytes(&mut salt);
    rand::thread_rng().fill_bytes(&mut nonce_bytes);

    // Derive key from password using PBKDF2-HMAC-SHA256
    let mut key = [0u8; 32];
    pbkdf2_hmac::<Sha256>(password.as_bytes(), &salt, PBKDF2_ITERATIONS, &mut key);

    // Encrypt with AES-256-GCM
    let cipher = Aes256Gcm::new_from_slice(&key)
        .map_err(|e| AvaError::ConfigError(format!("AES key init failed: {e}")))?;
    let nonce = Nonce::from_slice(&nonce_bytes);
    let ciphertext = cipher
        .encrypt(nonce, plaintext.as_bytes())
        .map_err(|e| AvaError::ConfigError(format!("Encryption failed: {e}")))?;

    let b64 = base64::engine::general_purpose::STANDARD;
    Ok(EncryptedEnvelope {
        salt: b64.encode(salt),
        nonce: b64.encode(nonce_bytes),
        ciphertext: b64.encode(ciphertext),
    })
}

#[cfg(not(feature = "keychain"))]
fn encrypt_store(_store: &CredentialStore, _password: &str) -> Result<EncryptedEnvelope> {
    Err(AvaError::ConfigError(
        "Encrypted storage requires the 'keychain' feature".to_string(),
    ))
}

/// Decrypt an EncryptedEnvelope back into a CredentialStore.
#[cfg(feature = "keychain")]
fn decrypt_store(envelope: &EncryptedEnvelope, password: &str) -> Result<CredentialStore> {
    use aes_gcm::{aead::Aead, Aes256Gcm, KeyInit, Nonce};
    use base64::Engine;
    use pbkdf2::pbkdf2_hmac;
    use sha2::Sha256;

    let b64 = base64::engine::general_purpose::STANDARD;

    let salt = b64
        .decode(&envelope.salt)
        .map_err(|e| AvaError::SerializationError(format!("Invalid salt base64: {e}")))?;
    let nonce_bytes = b64
        .decode(&envelope.nonce)
        .map_err(|e| AvaError::SerializationError(format!("Invalid nonce base64: {e}")))?;
    let ciphertext = b64
        .decode(&envelope.ciphertext)
        .map_err(|e| AvaError::SerializationError(format!("Invalid ciphertext base64: {e}")))?;

    // Derive key
    let mut key = [0u8; 32];
    pbkdf2_hmac::<Sha256>(password.as_bytes(), &salt, PBKDF2_ITERATIONS, &mut key);

    // Decrypt
    let cipher = Aes256Gcm::new_from_slice(&key)
        .map_err(|e| AvaError::ConfigError(format!("AES key init failed: {e}")))?;
    let nonce = Nonce::from_slice(&nonce_bytes);
    let plaintext = cipher
        .decrypt(nonce, ciphertext.as_ref())
        .map_err(|_| {
            AvaError::ConfigError(
                "Decryption failed — wrong master password or corrupted file".to_string(),
            )
        })?;

    let store: CredentialStore = serde_json::from_slice(&plaintext)
        .map_err(|e| AvaError::SerializationError(format!("Invalid decrypted JSON: {e}")))?;

    Ok(store)
}

#[cfg(not(feature = "keychain"))]
fn decrypt_store(_envelope: &EncryptedEnvelope, _password: &str) -> Result<CredentialStore> {
    Err(AvaError::ConfigError(
        "Encrypted storage requires the 'keychain' feature".to_string(),
    ))
}

// ── Redaction helper ─────────────────────────────────────────────────────

/// Redact an API key for logging, showing only the last 4 characters.
///
/// Returns `****...abcd` format for keys longer than 8 chars, `****` for shorter keys.
pub fn redact_key_for_log(key: &str) -> String {
    let chars: Vec<char> = key.chars().collect();
    if chars.len() <= 4 {
        "****".to_string()
    } else {
        let suffix: String = chars[chars.len() - 4..].iter().collect();
        format!("****...{suffix}")
    }
}

// ── Tests ────────────────────────────────────────────────────────────────

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::Mutex;

    /// Tests that use AVA_MASTER_PASSWORD must hold this lock to avoid races.
    static ENV_LOCK: Mutex<()> = Mutex::new(());

    #[test]
    fn redact_key_for_log_short_keys() {
        assert_eq!(redact_key_for_log(""), "****");
        assert_eq!(redact_key_for_log("ab"), "****");
        assert_eq!(redact_key_for_log("abcd"), "****");
    }

    #[test]
    fn redact_key_for_log_long_keys() {
        assert_eq!(redact_key_for_log("abcde"), "****...bcde");
        assert_eq!(redact_key_for_log("sk-1234567890abcd"), "****...abcd");
        assert_eq!(
            redact_key_for_log("sk-ant-api03-long-key-here-WXYZ"),
            "****...WXYZ"
        );
    }

    #[cfg(feature = "keychain")]
    #[test]
    fn encrypt_decrypt_roundtrip() {
        let mut store = CredentialStore::default();
        store.set(
            "openai",
            ProviderCredential {
                api_key: "sk-test-roundtrip-1234".to_string(),
                base_url: None,
                org_id: None,
                oauth_token: None,
                oauth_refresh_token: None,
                oauth_expires_at: None,
                oauth_account_id: None,
            },
        );
        store.set(
            "anthropic",
            ProviderCredential {
                api_key: "sk-ant-test-5678".to_string(),
                base_url: Some("https://api.anthropic.com".to_string()),
                org_id: None,
                oauth_token: Some("oauth-token-abc".to_string()),
                oauth_refresh_token: Some("refresh-xyz".to_string()),
                oauth_expires_at: Some(1700000000),
                oauth_account_id: None,
            },
        );

        let password = "test-master-password-42";
        let envelope = encrypt_store(&store, password).unwrap();

        // Verify envelope fields are base64
        assert!(!envelope.salt.is_empty());
        assert!(!envelope.nonce.is_empty());
        assert!(!envelope.ciphertext.is_empty());

        // Decrypt and verify
        let decrypted = decrypt_store(&envelope, password).unwrap();
        assert_eq!(decrypted.providers.len(), 2);

        let openai = decrypted.providers.get("openai").unwrap();
        assert_eq!(openai.api_key, "sk-test-roundtrip-1234");

        let anthropic = decrypted.providers.get("anthropic").unwrap();
        assert_eq!(anthropic.api_key, "sk-ant-test-5678");
        assert_eq!(
            anthropic.base_url.as_deref(),
            Some("https://api.anthropic.com")
        );
        assert_eq!(anthropic.oauth_token.as_deref(), Some("oauth-token-abc"));
        assert_eq!(
            anthropic.oauth_refresh_token.as_deref(),
            Some("refresh-xyz")
        );
        assert_eq!(anthropic.oauth_expires_at, Some(1700000000));
    }

    #[cfg(feature = "keychain")]
    #[test]
    fn decrypt_with_wrong_password_fails() {
        let mut store = CredentialStore::default();
        store.set(
            "test",
            ProviderCredential {
                api_key: "secret-key".to_string(),
                base_url: None,
                org_id: None,
                oauth_token: None,
                oauth_refresh_token: None,
                oauth_expires_at: None,
                oauth_account_id: None,
            },
        );

        let envelope = encrypt_store(&store, "correct-password").unwrap();
        let result = decrypt_store(&envelope, "wrong-password");
        assert!(result.is_err());
        assert!(result
            .unwrap_err()
            .to_string()
            .contains("wrong master password"));
    }

    #[cfg(feature = "keychain")]
    #[test]
    fn encrypted_file_roundtrip() {
        let _guard = ENV_LOCK.lock().unwrap();
        let dir = tempfile::TempDir::new().unwrap();
        let enc_path = dir.path().join("credentials.enc");

        // Set master password via env for non-interactive test
        std::env::set_var("AVA_MASTER_PASSWORD", "test-file-roundtrip");

        let manager = KeychainManager::with_path(enc_path.clone());

        // Store credentials
        let cred = ProviderCredential {
            api_key: "sk-file-test-key".to_string(),
            base_url: None,
            org_id: None,
            oauth_token: None,
            oauth_refresh_token: None,
            oauth_expires_at: None,
            oauth_account_id: None,
        };
        manager.store("openrouter", &cred).unwrap();

        // Retrieve
        let retrieved = manager.retrieve("openrouter").unwrap().unwrap();
        assert_eq!(retrieved.api_key, "sk-file-test-key");

        // List
        let providers = manager.list_providers().unwrap();
        assert_eq!(providers, vec!["openrouter"]);

        // Delete
        assert!(manager.delete("openrouter").unwrap());
        assert!(manager.retrieve("openrouter").unwrap().is_none());

        std::env::remove_var("AVA_MASTER_PASSWORD");
    }

    #[cfg(feature = "keychain")]
    #[tokio::test]
    async fn migration_from_plaintext() {
        let _guard = ENV_LOCK.lock().unwrap();
        let dir = tempfile::TempDir::new().unwrap();
        let plaintext_path = dir.path().join("credentials.json");
        let enc_path = dir.path().join("credentials.enc");

        // Create a plaintext credentials file
        let mut store = CredentialStore::default();
        store.set(
            "openai",
            ProviderCredential {
                api_key: "sk-migrate-test".to_string(),
                base_url: None,
                org_id: None,
                oauth_token: None,
                oauth_refresh_token: None,
                oauth_expires_at: None,
                oauth_account_id: None,
            },
        );
        store.save(&plaintext_path).await.unwrap();

        std::env::set_var("AVA_MASTER_PASSWORD", "migration-test-pw");

        let manager = KeychainManager::with_path(enc_path);
        let result = manager.migrate_from_plaintext(&plaintext_path).await.unwrap();

        assert_eq!(result, MigrationResult::Migrated { count: 1 });
        assert!(!plaintext_path.exists(), "plaintext file should be deleted");

        // Verify the migrated credential
        let retrieved = manager.retrieve("openai").unwrap().unwrap();
        assert_eq!(retrieved.api_key, "sk-migrate-test");

        std::env::remove_var("AVA_MASTER_PASSWORD");
    }

    #[cfg(feature = "keychain")]
    #[tokio::test]
    async fn migration_no_file() {
        let dir = tempfile::TempDir::new().unwrap();
        let missing = dir.path().join("does-not-exist.json");
        let enc_path = dir.path().join("credentials.enc");

        let manager = KeychainManager::with_path(enc_path);
        let result = manager.migrate_from_plaintext(&missing).await.unwrap();
        assert_eq!(result, MigrationResult::NoFileFound);
    }

    #[cfg(feature = "keychain")]
    #[test]
    fn load_all_and_store_all() {
        let _guard = ENV_LOCK.lock().unwrap();
        let dir = tempfile::TempDir::new().unwrap();
        let enc_path = dir.path().join("credentials.enc");

        std::env::set_var("AVA_MASTER_PASSWORD", "bulk-test-pw");

        let manager = KeychainManager::with_path(enc_path);

        let mut store = CredentialStore::default();
        store.set(
            "anthropic",
            ProviderCredential {
                api_key: "sk-ant-bulk".to_string(),
                base_url: None,
                org_id: None,
                oauth_token: None,
                oauth_refresh_token: None,
                oauth_expires_at: None,
                oauth_account_id: None,
            },
        );
        store.set(
            "gemini",
            ProviderCredential {
                api_key: "gem-bulk-key".to_string(),
                base_url: None,
                org_id: None,
                oauth_token: None,
                oauth_refresh_token: None,
                oauth_expires_at: None,
                oauth_account_id: None,
            },
        );

        manager.store_all(&store).unwrap();

        let loaded = manager.load_all().unwrap();
        assert_eq!(loaded.providers.len(), 2);
        assert_eq!(
            loaded.providers.get("anthropic").unwrap().api_key,
            "sk-ant-bulk"
        );
        assert_eq!(
            loaded.providers.get("gemini").unwrap().api_key,
            "gem-bulk-key"
        );

        std::env::remove_var("AVA_MASTER_PASSWORD");
    }
}
