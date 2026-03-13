use std::collections::HashMap;
use std::path::{Path, PathBuf};
use std::pin::Pin;
use std::sync::Arc;

use async_trait::async_trait;
use ava_config::{CredentialStore, ProviderCredentialState};
use ava_types::{AvaError, Message, Result, StreamChunk, ThinkingLevel, Tool};
use futures::Stream;
use tokio::sync::{Mutex, RwLock};
use tracing::warn;

use crate::pool::ConnectionPool;
use crate::provider::{LLMProvider, LLMResponse};
use crate::providers::{common, create_provider};
use crate::thinking::ThinkingConfig;

type ProviderRefreshLock = Arc<Mutex<()>>;
type ProviderRefreshLocks = Arc<Mutex<HashMap<String, ProviderRefreshLock>>>;

pub(crate) struct DynamicCredentialProvider {
    provider_name: String,
    model: String,
    pool: Arc<ConnectionPool>,
    credentials: Arc<RwLock<CredentialStore>>,
    credentials_path: Option<PathBuf>,
    refresh_locks: ProviderRefreshLocks,
    metadata_supports_tools: bool,
    metadata_supports_thinking: bool,
    metadata_thinking_levels: Vec<ThinkingLevel>,
}

impl DynamicCredentialProvider {
    pub(crate) fn new(
        provider_name: impl Into<String>,
        model: impl Into<String>,
        pool: Arc<ConnectionPool>,
        credentials: Arc<RwLock<CredentialStore>>,
        credentials_path: Option<PathBuf>,
        refresh_locks: ProviderRefreshLocks,
        metadata_provider: &dyn LLMProvider,
    ) -> Self {
        Self {
            provider_name: provider_name.into(),
            model: model.into(),
            pool,
            credentials,
            credentials_path,
            refresh_locks,
            metadata_supports_tools: metadata_provider.supports_tools(),
            metadata_supports_thinking: metadata_provider.supports_thinking(),
            metadata_thinking_levels: metadata_provider.thinking_levels().to_vec(),
        }
    }

    async fn provider_for_request(&self) -> Result<Box<dyn LLMProvider>> {
        let snapshot = resolve_credentials_snapshot(
            &self.credentials,
            &self.provider_name,
            self.credentials_path.as_deref(),
            &self.refresh_locks,
        )
        .await?;

        create_provider(
            &self.provider_name,
            &self.model,
            &snapshot,
            self.pool.clone(),
        )
    }

    fn estimate_cost_for_family(&self, input_tokens: usize, output_tokens: usize) -> f64 {
        match self.provider_name.as_str() {
            "copilot" | "ollama" => 0.0,
            _ => {
                let (in_rate, out_rate) = common::model_pricing_usd_per_million(&self.model);
                common::estimate_cost_usd(input_tokens, output_tokens, in_rate, out_rate)
            }
        }
    }
}

pub(crate) async fn resolve_credentials_snapshot(
    credentials: &Arc<RwLock<CredentialStore>>,
    provider_name: &str,
    credentials_path: Option<&Path>,
    refresh_locks: &ProviderRefreshLocks,
) -> Result<CredentialStore> {
    let (snapshot, state) = {
        let credentials = credentials.read().await;
        (
            credentials.clone(),
            credentials.provider_credential_state(provider_name),
        )
    };

    let ProviderCredentialState::RefreshNeeded(_) = state else {
        return Ok(snapshot);
    };

    let provider_lock = refresh_lock(provider_name, refresh_locks).await;
    let _guard = provider_lock.lock().await;

    let (snapshot, state) = {
        let credentials = credentials.read().await;
        (
            credentials.clone(),
            credentials.provider_credential_state(provider_name),
        )
    };

    let ProviderCredentialState::RefreshNeeded(refresh) = state else {
        return Ok(snapshot);
    };

    let refreshed_tokens = match ava_auth::tokens::refresh_token(
        refresh.config,
        &refresh.refresh_token,
    )
    .await
    {
        Ok(tokens) => tokens,
        Err(error) if !refresh.existing.api_key.trim().is_empty() => {
            warn!(provider = provider_name, %error, "OAuth refresh failed; falling back to static API key");
            return Ok(snapshot);
        }
        Err(error) => {
            return Err(AvaError::ConfigError(format!(
                "Failed to refresh OAuth credential for {provider_name}: {error}"
            )))
        }
    };

    let updated_snapshot = {
        let mut credentials = credentials.write().await;
        let _refreshed = credentials.apply_refreshed_provider_tokens(
            provider_name,
            &refresh.existing,
            refreshed_tokens,
        );
        credentials.clone()
    };

    if let Some(path) = credentials_path {
        updated_snapshot.save(path).await?;
    }

    Ok(updated_snapshot)
}

async fn refresh_lock(
    provider_name: &str,
    refresh_locks: &ProviderRefreshLocks,
) -> ProviderRefreshLock {
    let mut locks = refresh_locks.lock().await;
    locks
        .entry(provider_name.to_string())
        .or_insert_with(|| Arc::new(Mutex::new(())))
        .clone()
}

#[async_trait]
impl LLMProvider for DynamicCredentialProvider {
    async fn generate(&self, messages: &[Message]) -> Result<String> {
        self.provider_for_request().await?.generate(messages).await
    }

    async fn generate_stream(
        &self,
        messages: &[Message],
    ) -> Result<Pin<Box<dyn Stream<Item = StreamChunk> + Send>>> {
        self.provider_for_request()
            .await?
            .generate_stream(messages)
            .await
    }

    fn estimate_tokens(&self, input: &str) -> usize {
        common::estimate_tokens(input)
    }

    fn estimate_cost(&self, input_tokens: usize, output_tokens: usize) -> f64 {
        self.estimate_cost_for_family(input_tokens, output_tokens)
    }

    fn model_name(&self) -> &str {
        &self.model
    }

    fn supports_tools(&self) -> bool {
        self.metadata_supports_tools
    }

    async fn generate_with_tools(
        &self,
        messages: &[Message],
        tools: &[Tool],
    ) -> Result<LLMResponse> {
        self.provider_for_request()
            .await?
            .generate_with_tools(messages, tools)
            .await
    }

    fn supports_thinking(&self) -> bool {
        self.metadata_supports_thinking
    }

    fn thinking_levels(&self) -> &[ThinkingLevel] {
        &self.metadata_thinking_levels
    }

    async fn generate_with_thinking(
        &self,
        messages: &[Message],
        tools: &[Tool],
        thinking: ThinkingLevel,
    ) -> Result<LLMResponse> {
        self.provider_for_request()
            .await?
            .generate_with_thinking(messages, tools, thinking)
            .await
    }

    async fn generate_stream_with_tools(
        &self,
        messages: &[Message],
        tools: &[Tool],
    ) -> Result<Pin<Box<dyn Stream<Item = StreamChunk> + Send>>> {
        self.provider_for_request()
            .await?
            .generate_stream_with_tools(messages, tools)
            .await
    }

    async fn generate_stream_with_thinking(
        &self,
        messages: &[Message],
        tools: &[Tool],
        thinking: ThinkingLevel,
    ) -> Result<Pin<Box<dyn Stream<Item = StreamChunk> + Send>>> {
        self.provider_for_request()
            .await?
            .generate_stream_with_thinking(messages, tools, thinking)
            .await
    }

    async fn generate_with_thinking_config(
        &self,
        messages: &[Message],
        tools: &[Tool],
        config: ThinkingConfig,
    ) -> Result<LLMResponse> {
        self.provider_for_request()
            .await?
            .generate_with_thinking_config(messages, tools, config)
            .await
    }

    async fn generate_stream_with_thinking_config(
        &self,
        messages: &[Message],
        tools: &[Tool],
        config: ThinkingConfig,
    ) -> Result<Pin<Box<dyn Stream<Item = StreamChunk> + Send>>> {
        self.provider_for_request()
            .await?
            .generate_stream_with_thinking_config(messages, tools, config)
            .await
    }
}
