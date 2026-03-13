use std::sync::Arc;
use std::time::Instant;

use ava_config::CredentialStore;
use ava_types::{Message, Role};

use crate::pool::ConnectionPool;
use crate::providers::create_provider;

const DEFAULT_TIMEOUT_SECS: u64 = 20;

pub fn default_model_for_provider(provider: &str) -> Option<&'static str> {
    match provider {
        "anthropic" => Some("claude-sonnet-4-20250514"),
        "openai" => Some("gpt-4o-mini"),
        "openrouter" => Some("openai/gpt-4o-mini"),
        "gemini" => Some("gemini-1.5-flash"),
        "ollama" => Some("llama3.1"),
        _ => None,
    }
}

pub async fn test_provider_credentials(
    provider: &str,
    model: &str,
    credentials: &CredentialStore,
) -> String {
    let pool = Arc::new(ConnectionPool::new());
    let provider_impl = match create_provider(provider, model, credentials, pool) {
        Ok(provider_impl) => provider_impl,
        Err(error) => {
            return format!("{provider}: FAIL ({error})");
        }
    };

    let start = Instant::now();
    let prompt = [Message::new(Role::User, "Hello")];
    let result = tokio::time::timeout(
        std::time::Duration::from_secs(DEFAULT_TIMEOUT_SECS),
        provider_impl.generate(&prompt),
    )
    .await;

    match result {
        Ok(Ok(_)) => format!(
            "{provider}: OK ({model} responded in {:.1}s)",
            start.elapsed().as_secs_f64()
        ),
        Ok(Err(error)) => format!("{provider}: FAIL ({error})"),
        Err(_) => format!("{provider}: FAIL (request timed out after {DEFAULT_TIMEOUT_SECS}s)"),
    }
}
