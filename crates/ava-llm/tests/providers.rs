use std::sync::Arc;

use ava_config::{CredentialStore, ProviderCredential};
use ava_llm::pool::ConnectionPool;
use ava_llm::provider::LLMProvider;
use ava_llm::providers::create_provider;
use ava_llm::providers::mock::MockProvider;
use ava_llm::providers::openai::OpenAIProvider;
use ava_llm::router::ModelRouter;
use ava_llm::{default_model_for_provider, test_provider_credentials};
use ava_types::{Message, Role};
use futures::StreamExt;
use serde_json::json;

fn credential(api_key: &str) -> ProviderCredential {
    ProviderCredential {
        api_key: api_key.to_string(),
        base_url: None,
        org_id: None,
        oauth_token: None,
        oauth_refresh_token: None,
        oauth_expires_at: None,
        oauth_account_id: None,
    }
}

fn pool() -> Arc<ConnectionPool> {
    Arc::new(ConnectionPool::new())
}

#[tokio::test]
async fn mock_provider_generate_and_stream() {
    let provider = MockProvider::new("mock", vec!["first".to_string(), "second".to_string()]);

    let generated = provider.generate(&[]).await.expect("generate should work");
    assert_eq!(generated, "first");

    let stream = provider
        .generate_stream(&[])
        .await
        .expect("stream should work");
    let parts: Vec<ava_types::StreamChunk> = stream.collect().await;
    assert_eq!(parts.len(), 1);
    assert_eq!(parts[0].text_content(), Some("second"));
}

#[test]
fn openai_request_body_serialization() {
    let provider = OpenAIProvider::new(pool(), "key", "gpt-4o-mini");
    let body = provider.build_request_body(&[Message::new(Role::User, "hello")], false);

    assert_eq!(body["model"], "gpt-4o-mini");
    assert_eq!(body["stream"], false);
    assert!(body["messages"].is_array());
}

#[test]
fn openai_bad_response_returns_error() {
    let payload = json!({"choices": []});
    let error = OpenAIProvider::parse_response_payload(&payload)
        .expect_err("bad payload should fail parsing");

    assert!(error
        .to_string()
        .contains("missing OpenAI completion choices"));
}

#[test]
fn token_and_cost_estimation_are_non_zero() {
    let provider = OpenAIProvider::new(pool(), "key", "gpt-4o-mini");
    let tokens = provider.estimate_tokens("estimate me");
    let cost = provider.estimate_cost(1500, 800);

    assert!(tokens > 0);
    assert!(cost > 0.0);
}

#[test]
fn create_provider_anthropic_succeeds_with_store_key() {
    let mut store = CredentialStore::default();
    store.set("anthropic", credential("sk-ant-1234"));

    let provider = create_provider("anthropic", "claude-sonnet-4-20250514", &store, pool())
        .expect("anthropic provider should be created");

    assert_eq!(provider.model_name(), "claude-sonnet-4-20250514");
}

#[test]
fn create_provider_anthropic_fails_without_key() {
    let store = CredentialStore::default();
    let error = create_provider("anthropic", "claude-sonnet-4-20250514", &store, pool())
        .err()
        .expect("missing key should fail");

    assert!(error.to_string().contains("anthropic"));
    assert!(error.to_string().contains("No API key"));
}

#[test]
fn create_provider_ollama_succeeds_without_key() {
    let store = CredentialStore::default();
    let provider = create_provider("ollama", "llama3.1", &store, pool())
        .expect("ollama should be created without key");

    assert_eq!(provider.model_name(), "llama3.1");
}

#[test]
fn create_provider_unknown_errors() {
    let store = CredentialStore::default();
    let error = create_provider("unknown-provider", "model", &store, pool())
        .err()
        .expect("unknown should fail");

    assert!(error.to_string().contains("unknown provider"));
}

#[test]
fn create_provider_base_url_override_openrouter() {
    let mut store = CredentialStore::default();
    store.set(
        "openrouter",
        ProviderCredential {
            api_key: "or-key-1234".to_string(),
            base_url: Some("https://openrouter.example/api".to_string()),
            org_id: None,
            oauth_token: None,
            oauth_refresh_token: None,
            oauth_expires_at: None,
            oauth_account_id: None,
        },
    );

    let provider = create_provider("openrouter", "openai/gpt-5", &store, pool())
        .expect("openrouter provider should be created");
    assert_eq!(provider.model_name(), "openai/gpt-5");
}

#[test]
fn create_provider_base_url_override_ollama() {
    let mut store = CredentialStore::default();
    store.set(
        "ollama",
        ProviderCredential {
            api_key: String::new(),
            base_url: Some("http://ollama.internal:11434".to_string()),
            org_id: None,
            oauth_token: None,
            oauth_refresh_token: None,
            oauth_expires_at: None,
            oauth_account_id: None,
        },
    );

    let provider = create_provider("ollama", "qwen2.5-coder", &store, pool())
        .expect("ollama provider should work");
    assert_eq!(provider.model_name(), "qwen2.5-coder");
}

#[tokio::test]
async fn router_returns_cached_provider_instance() {
    let mut store = CredentialStore::default();
    store.set("openai", credential("sk-openai-1234"));
    let router = ModelRouter::new(store);

    let first = router.route("openai", "gpt-4o-mini").await.unwrap();
    let second = router.route("openai", "gpt-4o-mini").await.unwrap();

    assert!(Arc::ptr_eq(&first, &second));
}

#[tokio::test]
async fn router_credential_update_invalidates_cache() {
    let mut store = CredentialStore::default();
    store.set("openai", credential("sk-openai-1"));
    let router = ModelRouter::new(store);

    let first = router.route("openai", "gpt-4o-mini").await.unwrap();
    assert_eq!(router.cache_size().await, 1);

    let mut updated = CredentialStore::default();
    updated.set("openai", credential("sk-openai-2"));
    router.update_credentials(updated).await;

    assert_eq!(router.cache_size().await, 0);
    let second = router.route("openai", "gpt-4o-mini").await.unwrap();

    assert!(!Arc::ptr_eq(&first, &second));
}

#[tokio::test]
async fn router_available_providers_matches_configured_credentials() {
    let mut store = CredentialStore::default();
    store.set("openai", credential("sk-openai"));
    store.set(
        "ollama",
        ProviderCredential {
            api_key: String::new(),
            base_url: Some("http://localhost:11434".to_string()),
            org_id: None,
            oauth_token: None,
            oauth_refresh_token: None,
            oauth_expires_at: None,
            oauth_account_id: None,
        },
    );
    let router = ModelRouter::new(store);

    let providers = router.available_providers().await;
    assert!(providers.contains(&"openai".to_string()));
    assert!(providers.contains(&"ollama".to_string()));
}

#[tokio::test]
async fn router_unconfigured_provider_returns_clear_error() {
    let router = ModelRouter::new(CredentialStore::default());
    let error = router
        .route("anthropic", "claude-sonnet-4-20250514")
        .await
        .err()
        .expect("missing anthro key should fail");

    assert!(error.to_string().contains("anthropic"));
    assert!(error.to_string().contains("No API key"));
}

#[test]
fn default_model_mapping_is_stable() {
    assert_eq!(default_model_for_provider("openai"), Some("gpt-4o-mini"));
    assert_eq!(
        default_model_for_provider("anthropic"),
        Some("claude-sonnet-4-20250514")
    );
    assert_eq!(default_model_for_provider("unknown"), None);
}

#[tokio::test]
async fn credential_test_reports_missing_key_as_fail_message() {
    let store = CredentialStore::default();
    let result = test_provider_credentials("anthropic", "claude-sonnet-4-20250514", &store).await;

    assert!(result.starts_with("anthropic: FAIL"));
}

#[tokio::test]
async fn router_with_pool_shares_connection_pool() {
    let mut store = CredentialStore::default();
    store.set("openai", credential("sk-openai-1234"));

    let shared_pool = Arc::new(ConnectionPool::new());
    let router = ModelRouter::with_pool(store, shared_pool.clone());

    // Verify the router uses the shared pool
    assert!(Arc::ptr_eq(router.pool(), &shared_pool));

    // Pool starts empty (clients are lazy)
    let stats = shared_pool.stats().await;
    assert_eq!(stats.active_clients, 0);

    // Manually trigger a client to verify the pool works
    let client = shared_pool.get_client("https://api.openai.com").await;
    let stats = shared_pool.stats().await;
    assert_eq!(stats.active_clients, 1);
    drop(client);
}
