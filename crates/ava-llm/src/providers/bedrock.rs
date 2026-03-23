use std::pin::Pin;
use std::sync::Arc;
use std::time::SystemTime;

use async_trait::async_trait;
use ava_types::{AvaError, Message, Result, StreamChunk, ThinkingLevel};
use futures::{Stream, StreamExt};
use serde_json::{json, Value};

use tracing::{debug, instrument, warn};

use crate::circuit_breaker::CircuitBreaker;
use crate::pool::ConnectionPool;
use crate::provider::{LLMProvider, LLMResponse, ProviderCapabilities};
use crate::providers::common;

/// AWS Bedrock provider for Anthropic Claude models (and others).
///
/// Uses the Anthropic Messages API format with AWS SigV4 request signing.
/// This implementation provides a basic SigV4 signing mechanism without
/// requiring the full AWS SDK.
///
/// # Credentials
///
/// ```json
/// {
///   "providers": {
///     "bedrock": {
///       "api_key": "AKIAIOSFODNN7EXAMPLE",
///       "oauth_token": "wJalrXUtnFEMI/K7MDENG/bPxRfiCYEXAMPLEKEY",
///       "org_id": "us-east-1",
///       "base_url": "https://bedrock-runtime.us-east-1.amazonaws.com"
///     }
///   }
/// }
/// ```
///
/// - `api_key`: AWS Access Key ID
/// - `oauth_token`: AWS Secret Access Key (stored in oauth_token field)
/// - `org_id`: AWS region (defaults to `us-east-1`)
/// - `base_url`: Optional custom endpoint override
#[derive(Clone)]
pub struct BedrockProvider {
    pool: Arc<ConnectionPool>,
    access_key_id: String,
    secret_access_key: String,
    region: String,
    model: String,
    base_url: String,
    max_tokens: usize,
    circuit_breaker: Option<Arc<CircuitBreaker>>,
}

impl BedrockProvider {
    pub fn new(
        pool: Arc<ConnectionPool>,
        access_key_id: impl Into<String>,
        secret_access_key: impl Into<String>,
        region: impl Into<String>,
        model: impl Into<String>,
    ) -> Self {
        let region = region.into();
        let base_url = format!("https://bedrock-runtime.{region}.amazonaws.com");
        Self {
            pool,
            access_key_id: access_key_id.into(),
            secret_access_key: secret_access_key.into(),
            region,
            model: model.into(),
            base_url,
            max_tokens: 4096,
            circuit_breaker: Some(Arc::new(CircuitBreaker::default_provider())),
        }
    }

    pub fn with_base_url(
        pool: Arc<ConnectionPool>,
        access_key_id: impl Into<String>,
        secret_access_key: impl Into<String>,
        region: impl Into<String>,
        model: impl Into<String>,
        base_url: impl Into<String>,
    ) -> Self {
        let region = region.into();
        Self {
            pool,
            access_key_id: access_key_id.into(),
            secret_access_key: secret_access_key.into(),
            region,
            model: model.into(),
            base_url: base_url.into().trim_end_matches('/').to_string(),
            max_tokens: 4096,
            circuit_breaker: Some(Arc::new(CircuitBreaker::default_provider())),
        }
    }

    /// The Bedrock invoke endpoint URL.
    ///
    /// Format: `{base_url}/model/{model-id}/invoke`
    fn invoke_url(&self) -> String {
        format!("{}/model/{}/invoke", self.base_url, self.model)
    }

    /// The Bedrock streaming invoke endpoint URL.
    ///
    /// Format: `{base_url}/model/{model-id}/invoke-with-response-stream`
    fn invoke_stream_url(&self) -> String {
        format!(
            "{}/model/{}/invoke-with-response-stream",
            self.base_url, self.model
        )
    }

    /// Build an Anthropic Messages API format request body (used by Bedrock).
    fn build_request_body(&self, messages: &[Message]) -> Value {
        let (system, mapped_messages) = common::map_messages_anthropic(messages);
        let mut body = json!({
            "anthropic_version": "bedrock-2023-05-31",
            "max_tokens": self.max_tokens,
            "messages": mapped_messages,
        });

        if let Some(system_message) = system {
            body["system"] = json!(system_message);
        }

        body
    }

    /// Build a request body with tool definitions.
    fn build_request_body_with_tools(
        &self,
        messages: &[Message],
        tools: &[ava_types::Tool],
    ) -> Value {
        let mut body = self.build_request_body(messages);
        if !tools.is_empty() {
            // No cache_control for Bedrock (third-party)
            body["tools"] = json!(common::tools_to_anthropic_format_cached(tools, false));
        }
        body
    }

    async fn client(&self) -> Result<Arc<reqwest::Client>> {
        self.pool.get_client(&self.base_url).await
    }

    /// Sign a request with AWS SigV4.
    ///
    /// This is a simplified SigV4 implementation for Bedrock's invoke endpoint.
    /// It computes the canonical request, string to sign, and signature per
    /// the AWS Signature Version 4 specification.
    fn sign_request(&self, method: &str, url: &str, body: &[u8]) -> Result<Vec<(String, String)>> {
        use std::fmt::Write;

        let now = SystemTime::now()
            .duration_since(SystemTime::UNIX_EPOCH)
            .map_err(|e| AvaError::ProviderError {
                provider: "bedrock".to_string(),
                message: format!("system time error: {e}"),
            })?;

        // Format timestamps
        let secs = now.as_secs();
        let datetime = format_iso8601(secs);
        let date = &datetime[..8];

        // Parse URL components (simple string parsing to avoid url crate dependency)
        let (host, path) = parse_url_host_path(url).ok_or_else(|| AvaError::ProviderError {
            provider: "bedrock".to_string(),
            message: format!("cannot parse host/path from URL: {url}"),
        })?;

        let service = "bedrock";
        let credential_scope = format!("{date}/{}/{service}/aws4_request", self.region);

        // Hash the payload
        let payload_hash = sha256_hex(body);

        // Canonical headers (must be sorted)
        let content_type = "application/json";
        let canonical_headers = format!(
            "content-type:{content_type}\nhost:{host}\nx-amz-content-sha256:{payload_hash}\nx-amz-date:{datetime}\n"
        );
        let signed_headers = "content-type;host;x-amz-content-sha256;x-amz-date";

        // Canonical request
        let canonical_request =
            format!("{method}\n{path}\n\n{canonical_headers}\n{signed_headers}\n{payload_hash}");

        // String to sign
        let canonical_hash = sha256_hex(canonical_request.as_bytes());
        let string_to_sign =
            format!("AWS4-HMAC-SHA256\n{datetime}\n{credential_scope}\n{canonical_hash}");

        // Signing key
        let k_date = hmac_sha256(
            format!("AWS4{}", self.secret_access_key).as_bytes(),
            date.as_bytes(),
        );
        let k_region = hmac_sha256(&k_date, self.region.as_bytes());
        let k_service = hmac_sha256(&k_region, service.as_bytes());
        let k_signing = hmac_sha256(&k_service, b"aws4_request");

        // Signature
        let signature_bytes = hmac_sha256(&k_signing, string_to_sign.as_bytes());
        let mut signature = String::with_capacity(64);
        for byte in &signature_bytes {
            write!(&mut signature, "{byte:02x}").unwrap();
        }

        let authorization = format!(
            "AWS4-HMAC-SHA256 Credential={}/{credential_scope}, SignedHeaders={signed_headers}, Signature={signature}",
            self.access_key_id
        );

        Ok(vec![
            ("Authorization".to_string(), authorization),
            ("x-amz-date".to_string(), datetime),
            ("x-amz-content-sha256".to_string(), payload_hash),
            ("Content-Type".to_string(), content_type.to_string()),
        ])
    }

    async fn send_request(&self, request: reqwest::RequestBuilder) -> Result<reqwest::Response> {
        common::send_with_retry_cb(
            request,
            "Bedrock",
            common::DEFAULT_MAX_RETRIES,
            self.circuit_breaker.as_deref(),
        )
        .await
    }
}

#[async_trait]
impl LLMProvider for BedrockProvider {
    #[instrument(skip(self, messages), fields(model = %self.model, region = %self.region))]
    async fn generate(&self, messages: &[Message]) -> Result<String> {
        let client = self.client().await?;
        let body = self.build_request_body(messages);
        let body_bytes =
            serde_json::to_vec(&body).map_err(|e| AvaError::SerializationError(e.to_string()))?;

        let url = self.invoke_url();
        let headers = self.sign_request("POST", &url, &body_bytes)?;

        let mut request = client.post(&url).body(body_bytes);
        for (key, value) in headers {
            request = request.header(&key, &value);
        }

        let response = self.send_request(request).await?;
        let response = common::validate_status(response, "Bedrock").await?;
        let payload: Value = response
            .json()
            .await
            .map_err(|error| AvaError::SerializationError(error.to_string()))?;

        debug!(provider = "Bedrock", "raw response payload: {payload}");
        common::parse_anthropic_completion_payload(&payload)
    }

    #[instrument(skip(self, messages), fields(model = %self.model, region = %self.region))]
    async fn generate_stream(
        &self,
        messages: &[Message],
    ) -> Result<Pin<Box<dyn Stream<Item = StreamChunk> + Send>>> {
        let client = self.client().await?;
        let body = self.build_request_body(messages);
        let body_bytes =
            serde_json::to_vec(&body).map_err(|e| AvaError::SerializationError(e.to_string()))?;

        let url = self.invoke_stream_url();
        let headers = self.sign_request("POST", &url, &body_bytes)?;

        let mut request = client.post(&url).body(body_bytes);
        for (key, value) in headers {
            request = request.header(&key, &value);
        }

        let response = self.send_request(request).await?;
        let response = common::validate_status(response, "Bedrock").await?;

        let mut sse_parser = common::SseParser::new();
        let stream = response.bytes_stream().flat_map(move |chunk| {
            let chunks = chunk
                .ok()
                .and_then(|bytes| String::from_utf8(bytes.to_vec()).ok())
                .map(|text| {
                    sse_parser
                        .feed(&text)
                        .into_iter()
                        .filter_map(|line| serde_json::from_str::<Value>(&line).ok())
                        .filter_map(|payload| common::parse_anthropic_stream_chunk(&payload))
                        .collect::<Vec<_>>()
                })
                .unwrap_or_default();

            futures::stream::iter(chunks)
        });

        Ok(Box::pin(stream))
    }

    fn estimate_tokens(&self, input: &str) -> usize {
        common::estimate_tokens(input)
    }

    fn estimate_cost(&self, input_tokens: usize, output_tokens: usize) -> f64 {
        let (in_rate, out_rate) = common::model_pricing_usd_per_million(&self.model);
        common::estimate_cost_usd(input_tokens, output_tokens, in_rate, out_rate)
    }

    fn model_name(&self) -> &str {
        &self.model
    }

    fn capabilities(&self) -> ProviderCapabilities {
        ProviderCapabilities {
            supports_streaming: true,
            supports_tool_use: true,
            supports_thinking: false,
            supports_thinking_levels: false,
            supports_images: true,
            max_context_window: 200_000,
            supports_prompt_caching: false,
            is_subscription: false,
        }
    }

    fn provider_kind(&self) -> crate::message_transform::ProviderKind {
        crate::message_transform::ProviderKind::Bedrock
    }

    fn supports_tools(&self) -> bool {
        true
    }

    #[instrument(skip(self, messages, tools), fields(model = %self.model, region = %self.region))]
    async fn generate_with_tools(
        &self,
        messages: &[Message],
        tools: &[ava_types::Tool],
    ) -> Result<LLMResponse> {
        let client = self.client().await?;
        let body = self.build_request_body_with_tools(messages, tools);
        let body_bytes =
            serde_json::to_vec(&body).map_err(|e| AvaError::SerializationError(e.to_string()))?;

        let url = self.invoke_url();
        let headers = self.sign_request("POST", &url, &body_bytes)?;

        let mut request = client.post(&url).body(body_bytes);
        for (key, value) in headers {
            request = request.header(&key, &value);
        }

        let response = self.send_request(request).await?;
        let response = common::validate_status(response, "Bedrock").await?;
        let payload: Value = response
            .json()
            .await
            .map_err(|error| AvaError::SerializationError(error.to_string()))?;

        debug!(provider = "Bedrock", "raw response payload: {payload}");

        let content = common::parse_anthropic_completion_payload(&payload).unwrap_or_else(|e| {
            warn!(provider = "Bedrock", "failed to parse completion: {e}");
            String::new()
        });
        let tool_calls = common::parse_anthropic_tool_calls(&payload);
        let usage = common::parse_usage(&payload);

        Ok(LLMResponse {
            content,
            tool_calls,
            usage,
            thinking: None,
        })
    }

    #[instrument(skip(self, messages, tools), fields(model = %self.model, region = %self.region))]
    async fn generate_stream_with_tools(
        &self,
        messages: &[Message],
        tools: &[ava_types::Tool],
    ) -> Result<Pin<Box<dyn Stream<Item = StreamChunk> + Send>>> {
        let client = self.client().await?;
        let body = self.build_request_body_with_tools(messages, tools);
        let body_bytes =
            serde_json::to_vec(&body).map_err(|e| AvaError::SerializationError(e.to_string()))?;

        let url = self.invoke_stream_url();
        let headers = self.sign_request("POST", &url, &body_bytes)?;

        let mut request = client.post(&url).body(body_bytes);
        for (key, value) in headers {
            request = request.header(&key, &value);
        }

        let response = self.send_request(request).await?;
        let response = common::validate_status(response, "Bedrock").await?;

        let mut sse_parser = common::SseParser::new();
        let stream = response.bytes_stream().flat_map(move |chunk| {
            let chunks = chunk
                .ok()
                .and_then(|bytes| String::from_utf8(bytes.to_vec()).ok())
                .map(|text| {
                    sse_parser
                        .feed(&text)
                        .into_iter()
                        .filter_map(|line| serde_json::from_str::<Value>(&line).ok())
                        .filter_map(|payload| common::parse_anthropic_stream_chunk(&payload))
                        .collect::<Vec<_>>()
                })
                .unwrap_or_default();

            futures::stream::iter(chunks)
        });

        Ok(Box::pin(stream))
    }

    fn supports_thinking(&self) -> bool {
        false
    }

    fn thinking_levels(&self) -> &[ThinkingLevel] {
        &[]
    }

    #[instrument(skip(self, messages, tools), fields(model = %self.model, thinking = ?thinking))]
    async fn generate_with_thinking(
        &self,
        messages: &[Message],
        tools: &[ava_types::Tool],
        thinking: ThinkingLevel,
    ) -> Result<LLMResponse> {
        let _ = thinking;
        self.generate_with_tools(messages, tools).await
    }

    #[instrument(skip(self, messages, tools), fields(model = %self.model, thinking = ?thinking))]
    async fn generate_stream_with_thinking(
        &self,
        messages: &[Message],
        tools: &[ava_types::Tool],
        thinking: ThinkingLevel,
    ) -> Result<Pin<Box<dyn Stream<Item = StreamChunk> + Send>>> {
        let _ = thinking;
        self.generate_stream_with_tools(messages, tools).await
    }
}

// ---------------------------------------------------------------------------
// Minimal crypto helpers for SigV4 signing (no external dependency required)
// ---------------------------------------------------------------------------

/// Parse host and path from a URL string without the `url` crate.
///
/// Only supports `https://host/path` format (which is all Bedrock needs).
fn parse_url_host_path(url: &str) -> Option<(String, String)> {
    let without_scheme = url
        .strip_prefix("https://")
        .or_else(|| url.strip_prefix("http://"))?;
    let (host, path) = match without_scheme.find('/') {
        Some(idx) => (
            without_scheme[..idx].to_string(),
            without_scheme[idx..].to_string(),
        ),
        None => (without_scheme.to_string(), "/".to_string()),
    };
    // Strip port from host if present
    let host = match host.find(':') {
        Some(idx) => host[..idx].to_string(),
        None => host,
    };
    Some((host, path))
}

/// Format a Unix timestamp as an ISO 8601 basic datetime (e.g., `20260322T120000Z`).
fn format_iso8601(secs: u64) -> String {
    // Convert epoch seconds to date/time components
    let time_of_day = secs % 86400;
    let hours = time_of_day / 3600;
    let minutes = (time_of_day % 3600) / 60;
    let seconds = time_of_day % 60;

    // Days since Unix epoch (1970-01-01) to calendar date
    let days = secs / 86400;
    let (year, month, day) = epoch_days_to_date(days as i64);

    format!("{year:04}{month:02}{day:02}T{hours:02}{minutes:02}{seconds:02}Z")
}

/// Convert days since Unix epoch to (year, month, day).
fn epoch_days_to_date(days: i64) -> (i32, u32, u32) {
    // Algorithm from Howard Hinnant's chrono-compatible date library
    let z = days + 719468;
    let era = if z >= 0 { z } else { z - 146096 } / 146097;
    let doe = (z - era * 146097) as u32;
    let yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    let y = yoe as i64 + era * 400;
    let doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    let mp = (5 * doy + 2) / 153;
    let d = doy - (153 * mp + 2) / 5 + 1;
    let m = if mp < 10 { mp + 3 } else { mp - 9 };
    let y = if m <= 2 { y + 1 } else { y };
    (y as i32, m, d)
}

/// Compute SHA-256 hash and return hex-encoded string.
fn sha256_hex(data: &[u8]) -> String {
    use std::fmt::Write;
    let hash = sha256(data);
    let mut hex = String::with_capacity(64);
    for byte in &hash {
        write!(&mut hex, "{byte:02x}").unwrap();
    }
    hex
}

/// Compute HMAC-SHA256.
fn hmac_sha256(key: &[u8], data: &[u8]) -> [u8; 32] {
    let block_size = 64;

    // If key is longer than block size, hash it
    let key = if key.len() > block_size {
        sha256(key).to_vec()
    } else {
        key.to_vec()
    };

    // Pad key to block size
    let mut ipad = vec![0x36u8; block_size];
    let mut opad = vec![0x5cu8; block_size];
    for (i, &k) in key.iter().enumerate() {
        ipad[i] ^= k;
        opad[i] ^= k;
    }

    // Inner hash: H(ipad || data)
    let mut inner = ipad;
    inner.extend_from_slice(data);
    let inner_hash = sha256(&inner);

    // Outer hash: H(opad || inner_hash)
    let mut outer = opad;
    outer.extend_from_slice(&inner_hash);
    sha256(&outer)
}

/// Minimal SHA-256 implementation.
///
/// Uses the standard FIPS 180-4 algorithm. This avoids an external crate
/// dependency for what is a small, self-contained crypto primitive used
/// only for AWS SigV4 request signing.
fn sha256(data: &[u8]) -> [u8; 32] {
    let h0: [u32; 8] = [
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab,
        0x5be0cd19,
    ];

    let k: [u32; 64] = [
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4,
        0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe,
        0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f,
        0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
        0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
        0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
        0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116,
        0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
        0xc67178f2,
    ];

    // Pre-processing: pad the message
    let bit_len = (data.len() as u64) * 8;
    let mut msg = data.to_vec();
    msg.push(0x80);
    while (msg.len() % 64) != 56 {
        msg.push(0);
    }
    msg.extend_from_slice(&bit_len.to_be_bytes());

    let mut hash = h0;

    // Process each 512-bit (64-byte) block
    for block in msg.chunks(64) {
        let mut w = [0u32; 64];
        for i in 0..16 {
            w[i] = u32::from_be_bytes([
                block[4 * i],
                block[4 * i + 1],
                block[4 * i + 2],
                block[4 * i + 3],
            ]);
        }
        for i in 16..64 {
            let s0 = w[i - 15].rotate_right(7) ^ w[i - 15].rotate_right(18) ^ (w[i - 15] >> 3);
            let s1 = w[i - 2].rotate_right(17) ^ w[i - 2].rotate_right(19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16]
                .wrapping_add(s0)
                .wrapping_add(w[i - 7])
                .wrapping_add(s1);
        }

        let [mut a, mut b, mut c, mut d, mut e, mut f, mut g, mut hh] = hash;

        for i in 0..64 {
            let s1 = e.rotate_right(6) ^ e.rotate_right(11) ^ e.rotate_right(25);
            let ch = (e & f) ^ ((!e) & g);
            let temp1 = hh
                .wrapping_add(s1)
                .wrapping_add(ch)
                .wrapping_add(k[i])
                .wrapping_add(w[i]);
            let s0 = a.rotate_right(2) ^ a.rotate_right(13) ^ a.rotate_right(22);
            let maj = (a & b) ^ (a & c) ^ (b & c);
            let temp2 = s0.wrapping_add(maj);

            hh = g;
            g = f;
            f = e;
            e = d.wrapping_add(temp1);
            d = c;
            c = b;
            b = a;
            a = temp1.wrapping_add(temp2);
        }

        hash[0] = hash[0].wrapping_add(a);
        hash[1] = hash[1].wrapping_add(b);
        hash[2] = hash[2].wrapping_add(c);
        hash[3] = hash[3].wrapping_add(d);
        hash[4] = hash[4].wrapping_add(e);
        hash[5] = hash[5].wrapping_add(f);
        hash[6] = hash[6].wrapping_add(g);
        hash[7] = hash[7].wrapping_add(hh);
    }

    let mut result = [0u8; 32];
    for (i, &h) in hash.iter().enumerate() {
        result[4 * i..4 * i + 4].copy_from_slice(&h.to_be_bytes());
    }
    result
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::pool::ConnectionPool;

    fn pool() -> Arc<ConnectionPool> {
        Arc::new(ConnectionPool::new())
    }

    #[test]
    fn invoke_url_format() {
        let provider = BedrockProvider::new(
            pool(),
            "AKIAIOSFODNN7EXAMPLE",
            "wJalrXUtnFEMI/K7MDENG/bPxRfiCYEXAMPLEKEY",
            "us-east-1",
            "anthropic.claude-sonnet-4-20250514-v1:0",
        );
        assert_eq!(
            provider.invoke_url(),
            "https://bedrock-runtime.us-east-1.amazonaws.com/model/anthropic.claude-sonnet-4-20250514-v1:0/invoke"
        );
    }

    #[test]
    fn invoke_stream_url_format() {
        let provider = BedrockProvider::new(
            pool(),
            "AKIAIOSFODNN7EXAMPLE",
            "secret",
            "us-west-2",
            "anthropic.claude-sonnet-4-20250514-v1:0",
        );
        assert_eq!(
            provider.invoke_stream_url(),
            "https://bedrock-runtime.us-west-2.amazonaws.com/model/anthropic.claude-sonnet-4-20250514-v1:0/invoke-with-response-stream"
        );
    }

    #[test]
    fn custom_base_url() {
        let provider = BedrockProvider::with_base_url(
            pool(),
            "AKID",
            "secret",
            "us-east-1",
            "model-id",
            "https://custom-bedrock.example.com",
        );
        assert_eq!(
            provider.invoke_url(),
            "https://custom-bedrock.example.com/model/model-id/invoke"
        );
    }

    #[test]
    fn model_name_returns_model() {
        let provider = BedrockProvider::new(
            pool(),
            "AKID",
            "secret",
            "us-east-1",
            "anthropic.claude-sonnet-4-20250514-v1:0",
        );
        assert_eq!(
            provider.model_name(),
            "anthropic.claude-sonnet-4-20250514-v1:0"
        );
    }

    #[test]
    fn supports_tools_returns_true() {
        let provider = BedrockProvider::new(pool(), "AKID", "secret", "us-east-1", "model");
        assert!(provider.supports_tools());
    }

    #[test]
    fn does_not_support_thinking() {
        let provider = BedrockProvider::new(pool(), "AKID", "secret", "us-east-1", "model");
        assert!(!provider.supports_thinking());
        assert!(provider.thinking_levels().is_empty());
    }

    #[test]
    fn build_request_body_uses_anthropic_format() {
        let provider = BedrockProvider::new(pool(), "AKID", "secret", "us-east-1", "model");
        let messages = vec![
            Message::new(ava_types::Role::System, "You are helpful."),
            Message::new(ava_types::Role::User, "hello"),
        ];
        let body = provider.build_request_body(&messages);

        assert_eq!(body["anthropic_version"], json!("bedrock-2023-05-31"));
        assert!(body.get("messages").is_some());
        assert_eq!(body["system"], json!("You are helpful."));
        assert_eq!(body["max_tokens"], json!(4096));
    }

    #[test]
    fn sha256_empty_string() {
        let hash = sha256_hex(b"");
        assert_eq!(
            hash,
            "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"
        );
    }

    #[test]
    fn sha256_hello() {
        let hash = sha256_hex(b"hello");
        assert_eq!(
            hash,
            "2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824"
        );
    }

    #[test]
    fn hmac_sha256_test_vector() {
        // RFC 4231 Test Case 2
        let key = b"Jefe";
        let data = b"what do ya want for nothing?";
        let result = hmac_sha256(key, data);
        let hex: String = result.iter().map(|b| format!("{b:02x}")).collect();
        assert_eq!(
            hex,
            "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843"
        );
    }

    #[test]
    fn format_iso8601_known_date() {
        // 2026-03-21T00:00:00Z = 1774051200 seconds since epoch
        let result = format_iso8601(1774051200);
        assert_eq!(result, "20260321T000000Z");
    }

    #[test]
    fn sign_request_produces_authorization_header() {
        let provider = BedrockProvider::new(
            pool(),
            "AKIAIOSFODNN7EXAMPLE",
            "wJalrXUtnFEMI/K7MDENG/bPxRfiCYEXAMPLEKEY",
            "us-east-1",
            "anthropic.claude-sonnet-4-20250514-v1:0",
        );
        let body = b"{}";
        let url = provider.invoke_url();
        let headers = provider.sign_request("POST", &url, body).unwrap();

        // Should have Authorization, x-amz-date, x-amz-content-sha256, Content-Type
        let header_names: Vec<&str> = headers.iter().map(|(k, _)| k.as_str()).collect();
        assert!(header_names.contains(&"Authorization"));
        assert!(header_names.contains(&"x-amz-date"));
        assert!(header_names.contains(&"x-amz-content-sha256"));
        assert!(header_names.contains(&"Content-Type"));

        // Authorization should start with AWS4-HMAC-SHA256
        let auth = headers.iter().find(|(k, _)| k == "Authorization").unwrap();
        assert!(
            auth.1.starts_with("AWS4-HMAC-SHA256"),
            "Authorization header should start with AWS4-HMAC-SHA256, got: {}",
            auth.1
        );
        assert!(auth.1.contains("AKIAIOSFODNN7EXAMPLE"));
    }

    #[test]
    fn provider_kind_is_bedrock() {
        let provider = BedrockProvider::new(pool(), "AKID", "secret", "us-east-1", "model");
        assert_eq!(
            provider.provider_kind(),
            crate::message_transform::ProviderKind::Bedrock
        );
    }

    #[test]
    fn parse_url_host_path_basic() {
        let (host, path) = parse_url_host_path(
            "https://bedrock-runtime.us-east-1.amazonaws.com/model/test/invoke",
        )
        .unwrap();
        assert_eq!(host, "bedrock-runtime.us-east-1.amazonaws.com");
        assert_eq!(path, "/model/test/invoke");
    }

    #[test]
    fn parse_url_host_path_no_path() {
        let (host, path) =
            parse_url_host_path("https://bedrock-runtime.us-east-1.amazonaws.com").unwrap();
        assert_eq!(host, "bedrock-runtime.us-east-1.amazonaws.com");
        assert_eq!(path, "/");
    }

    #[test]
    fn parse_url_host_path_with_port() {
        let (host, path) = parse_url_host_path("https://localhost:8080/model/test").unwrap();
        assert_eq!(host, "localhost");
        assert_eq!(path, "/model/test");
    }
}
