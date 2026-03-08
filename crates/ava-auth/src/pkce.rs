//! PKCE (Proof Key for Code Exchange) generation.
//!
//! Generates cryptographically secure code verifier, challenge, and state parameters
//! per RFC 7636.

use base64::engine::general_purpose::URL_SAFE_NO_PAD;
use base64::Engine;
use sha2::{Digest, Sha256};

/// PKCE parameters for an OAuth authorization request.
#[derive(Debug, Clone)]
pub struct PkceParams {
    /// Code verifier — 64 random bytes base64url-encoded (~86 chars).
    pub verifier: String,
    /// Code challenge — SHA-256(verifier) base64url-encoded.
    pub challenge: String,
    /// Random state parameter for CSRF protection — 32 random bytes base64url-encoded.
    pub state: String,
}

/// Generate cryptographically secure PKCE parameters.
pub fn generate_pkce() -> PkceParams {
    // Code verifier: 64 random bytes → base64url (no padding)
    let verifier_bytes: [u8; 64] = rand::random();
    let verifier = URL_SAFE_NO_PAD.encode(verifier_bytes);

    // Code challenge: SHA-256(verifier) → base64url (no padding)
    let digest = Sha256::digest(verifier.as_bytes());
    let challenge = URL_SAFE_NO_PAD.encode(digest);

    // State: 32 random bytes → base64url (no padding)
    let state_bytes: [u8; 32] = rand::random();
    let state = URL_SAFE_NO_PAD.encode(state_bytes);

    PkceParams {
        verifier,
        challenge,
        state,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn pkce_generates_valid_lengths() {
        let pkce = generate_pkce();

        // Verifier: 64 bytes → 86 chars base64url
        assert_eq!(pkce.verifier.len(), 86);
        // Challenge: 32 bytes (SHA-256) → 43 chars base64url
        assert_eq!(pkce.challenge.len(), 43);
        // State: 32 bytes → 43 chars base64url
        assert_eq!(pkce.state.len(), 43);
    }

    #[test]
    fn pkce_challenge_matches_verifier() {
        let pkce = generate_pkce();

        // Recompute challenge from verifier
        let digest = Sha256::digest(pkce.verifier.as_bytes());
        let expected_challenge = URL_SAFE_NO_PAD.encode(digest);

        assert_eq!(pkce.challenge, expected_challenge);
    }

    #[test]
    fn pkce_generates_unique_values() {
        let a = generate_pkce();
        let b = generate_pkce();

        assert_ne!(a.verifier, b.verifier);
        assert_ne!(a.state, b.state);
    }

    #[test]
    fn pkce_uses_url_safe_chars() {
        let pkce = generate_pkce();
        let is_url_safe = |s: &str| {
            s.chars()
                .all(|c| c.is_ascii_alphanumeric() || c == '-' || c == '_')
        };

        assert!(is_url_safe(&pkce.verifier));
        assert!(is_url_safe(&pkce.challenge));
        assert!(is_url_safe(&pkce.state));
    }
}
