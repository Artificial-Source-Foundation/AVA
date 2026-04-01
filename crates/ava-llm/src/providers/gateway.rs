//! AI gateway detection from HTTP response headers (F26).
//!
//! Many teams route LLM requests through proxy gateways (LiteLLM, Helicone,
//! Portkey, Cloudflare AI Gateway, etc.). Detecting the gateway helps with
//! error parsing, rate-limit attribution, and telemetry.

use reqwest::header::HeaderMap;

/// Information about a detected AI gateway/proxy.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct GatewayInfo {
    /// Human-readable gateway name (e.g., "LiteLLM", "Helicone").
    pub name: String,
    /// Version string if available from the gateway headers.
    pub version: Option<String>,
}

/// Detect an AI gateway from HTTP response headers.
///
/// Returns `Some(GatewayInfo)` if a known gateway signature is found in the
/// headers, `None` otherwise. Detection is ordered by specificity — more
/// unique headers are checked first.
pub fn detect_gateway(headers: &HeaderMap) -> Option<GatewayInfo> {
    // 1. LiteLLM: x-litellm-version header
    if let Some(version) = headers
        .get("x-litellm-version")
        .and_then(|v| v.to_str().ok())
    {
        let info = GatewayInfo {
            name: "LiteLLM".to_string(),
            version: Some(version.to_string()),
        };
        tracing::info!(gateway = %info.name, version = ?info.version, "F26: AI gateway detected");
        return Some(info);
    }

    // 2. Helicone: helicone-id header
    if headers.contains_key("helicone-id") {
        let info = GatewayInfo {
            name: "Helicone".to_string(),
            version: None,
        };
        tracing::info!(gateway = %info.name, version = ?info.version, "F26: AI gateway detected");
        return Some(info);
    }

    // 3. Portkey: x-portkey-gateway-id header
    if headers.contains_key("x-portkey-gateway-id") {
        let info = GatewayInfo {
            name: "Portkey".to_string(),
            version: None,
        };
        tracing::info!(gateway = %info.name, version = ?info.version, "F26: AI gateway detected");
        return Some(info);
    }

    // 4. Cloudflare AI Gateway: cf-aig-cache-status or any cf-aig-* header
    if headers.contains_key("cf-aig-cache-status")
        || headers.keys().any(|k| k.as_str().starts_with("cf-aig-"))
    {
        let info = GatewayInfo {
            name: "Cloudflare AI Gateway".to_string(),
            version: None,
        };
        tracing::info!(gateway = %info.name, version = ?info.version, "F26: AI gateway detected");
        return Some(info);
    }

    // 5. BrainTrust: x-bt-org-id header
    if headers.contains_key("x-bt-org-id") {
        let info = GatewayInfo {
            name: "BrainTrust".to_string(),
            version: None,
        };
        tracing::info!(gateway = %info.name, version = ?info.version, "F26: AI gateway detected");
        return Some(info);
    }

    // 6. Vercel AI: x-vercel-ai-provider header
    if headers.contains_key("x-vercel-ai-provider") {
        let info = GatewayInfo {
            name: "Vercel AI".to_string(),
            version: None,
        };
        tracing::info!(gateway = %info.name, version = ?info.version, "F26: AI gateway detected");
        return Some(info);
    }

    // 7. LangSmith: x-langsmith-trace header
    if headers.contains_key("x-langsmith-trace") {
        let info = GatewayInfo {
            name: "LangSmith".to_string(),
            version: None,
        };
        tracing::info!(gateway = %info.name, version = ?info.version, "F26: AI gateway detected");
        return Some(info);
    }

    None
}

#[cfg(test)]
mod tests {
    use super::*;
    use reqwest::header::{HeaderMap, HeaderName, HeaderValue};

    fn headers_with(pairs: &[(&str, &str)]) -> HeaderMap {
        let mut map = HeaderMap::new();
        for (k, v) in pairs {
            map.insert(
                HeaderName::from_bytes(k.as_bytes()).unwrap(),
                HeaderValue::from_str(v).unwrap(),
            );
        }
        map
    }

    #[test]
    fn detect_litellm() {
        let headers = headers_with(&[("x-litellm-version", "1.35.2")]);
        let info = detect_gateway(&headers).unwrap();
        assert_eq!(info.name, "LiteLLM");
        assert_eq!(info.version.as_deref(), Some("1.35.2"));
    }

    #[test]
    fn detect_helicone() {
        let headers = headers_with(&[("helicone-id", "abc-123")]);
        let info = detect_gateway(&headers).unwrap();
        assert_eq!(info.name, "Helicone");
        assert!(info.version.is_none());
    }

    #[test]
    fn detect_portkey() {
        let headers = headers_with(&[("x-portkey-gateway-id", "gw-xyz")]);
        let info = detect_gateway(&headers).unwrap();
        assert_eq!(info.name, "Portkey");
    }

    #[test]
    fn detect_cloudflare_aig_cache() {
        let headers = headers_with(&[("cf-aig-cache-status", "HIT")]);
        let info = detect_gateway(&headers).unwrap();
        assert_eq!(info.name, "Cloudflare AI Gateway");
    }

    #[test]
    fn detect_cloudflare_aig_other() {
        let headers = headers_with(&[("cf-aig-request-id", "req-123")]);
        let info = detect_gateway(&headers).unwrap();
        assert_eq!(info.name, "Cloudflare AI Gateway");
    }

    #[test]
    fn detect_braintrust() {
        let headers = headers_with(&[("x-bt-org-id", "org-456")]);
        let info = detect_gateway(&headers).unwrap();
        assert_eq!(info.name, "BrainTrust");
    }

    #[test]
    fn detect_vercel_ai() {
        let headers = headers_with(&[("x-vercel-ai-provider", "openai")]);
        let info = detect_gateway(&headers).unwrap();
        assert_eq!(info.name, "Vercel AI");
    }

    #[test]
    fn detect_langsmith() {
        let headers = headers_with(&[("x-langsmith-trace", "trace-789")]);
        let info = detect_gateway(&headers).unwrap();
        assert_eq!(info.name, "LangSmith");
    }

    #[test]
    fn no_gateway_returns_none() {
        let headers = headers_with(&[("content-type", "application/json")]);
        assert!(detect_gateway(&headers).is_none());
    }

    #[test]
    fn empty_headers_returns_none() {
        let headers = HeaderMap::new();
        assert!(detect_gateway(&headers).is_none());
    }

    #[test]
    fn unknown_headers_ignored() {
        let headers = headers_with(&[("x-custom-gateway", "my-gateway"), ("x-request-id", "abc")]);
        assert!(detect_gateway(&headers).is_none());
    }
}
