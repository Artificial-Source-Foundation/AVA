use std::collections::HashMap;
use std::path::{Path, PathBuf};
use std::pin::Pin;
use std::sync::Arc;

use async_trait::async_trait;
use ava_config::{CredentialStore, ProviderCredentialState};
use ava_types::{AvaError, Message, Result, StreamChunk, ThinkingLevel, Tool};
use futures::Stream;
use tokio::sync::{Mutex, RwLock};
use tracing::{info, warn};

use crate::pool::ConnectionPool;
use crate::provider::{LLMProvider, LLMResponse, ProviderCapabilities};
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
    metadata_capabilities: ProviderCapabilities,
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
            metadata_capabilities: metadata_provider.capabilities(),
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

    /// Whether this provider uses OAuth credentials that can be refreshed.
    fn is_oauth_provider(&self) -> bool {
        ava_auth::config::oauth_config(&self.provider_name).is_some()
    }

    /// Force-refresh OAuth credentials by expiring the cached token, then
    /// resolve fresh credentials and build a new provider.
    async fn provider_after_forced_refresh(&self) -> Result<Box<dyn LLMProvider>> {
        // Mark the current token as expired so resolve_credentials_snapshot
        // will trigger a refresh.
        {
            let mut store = self.credentials.write().await;
            if let Some(cred) = store.providers.get_mut(&self.provider_name) {
                if cred.is_oauth_configured() {
                    // Set expiry to 0 (epoch) so is_oauth_expired() returns true
                    cred.oauth_expires_at = Some(0);
                }
            }
        }

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

    /// Check whether an error is an auth failure (401) that could be resolved
    /// by refreshing OAuth credentials.
    fn is_refreshable_auth_error(err: &AvaError) -> bool {
        matches!(err, AvaError::MissingApiKey { .. })
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
        let result = self.provider_for_request().await?.generate(messages).await;
        if self.is_oauth_provider() && result.as_ref().is_err_and(Self::is_refreshable_auth_error) {
            info!(provider = %self.provider_name, "401 received; forcing OAuth token refresh and retrying");
            return self
                .provider_after_forced_refresh()
                .await?
                .generate(messages)
                .await;
        }
        result
    }

    async fn generate_stream(
        &self,
        messages: &[Message],
    ) -> Result<Pin<Box<dyn Stream<Item = StreamChunk> + Send>>> {
        let result = self
            .provider_for_request()
            .await?
            .generate_stream(messages)
            .await;
        if self.is_oauth_provider() && result.as_ref().is_err_and(Self::is_refreshable_auth_error) {
            info!(provider = %self.provider_name, "401 received; forcing OAuth token refresh and retrying");
            return self
                .provider_after_forced_refresh()
                .await?
                .generate_stream(messages)
                .await;
        }
        result
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

    fn capabilities(&self) -> ProviderCapabilities {
        self.metadata_capabilities.clone()
    }

    fn supports_tools(&self) -> bool {
        self.metadata_supports_tools
    }

    async fn generate_with_tools(
        &self,
        messages: &[Message],
        tools: &[Tool],
    ) -> Result<LLMResponse> {
        let result = self
            .provider_for_request()
            .await?
            .generate_with_tools(messages, tools)
            .await;
        if self.is_oauth_provider() && result.as_ref().is_err_and(Self::is_refreshable_auth_error) {
            info!(provider = %self.provider_name, "401 received; forcing OAuth token refresh and retrying");
            return self
                .provider_after_forced_refresh()
                .await?
                .generate_with_tools(messages, tools)
                .await;
        }
        result
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
        let result = self
            .provider_for_request()
            .await?
            .generate_with_thinking(messages, tools, thinking)
            .await;
        if self.is_oauth_provider() && result.as_ref().is_err_and(Self::is_refreshable_auth_error) {
            info!(provider = %self.provider_name, "401 received; forcing OAuth token refresh and retrying");
            return self
                .provider_after_forced_refresh()
                .await?
                .generate_with_thinking(messages, tools, thinking)
                .await;
        }
        result
    }

    async fn generate_stream_with_tools(
        &self,
        messages: &[Message],
        tools: &[Tool],
    ) -> Result<Pin<Box<dyn Stream<Item = StreamChunk> + Send>>> {
        let result = self
            .provider_for_request()
            .await?
            .generate_stream_with_tools(messages, tools)
            .await;
        if self.is_oauth_provider() && result.as_ref().is_err_and(Self::is_refreshable_auth_error) {
            info!(provider = %self.provider_name, "401 received; forcing OAuth token refresh and retrying");
            return self
                .provider_after_forced_refresh()
                .await?
                .generate_stream_with_tools(messages, tools)
                .await;
        }
        result
    }

    async fn generate_stream_with_thinking(
        &self,
        messages: &[Message],
        tools: &[Tool],
        thinking: ThinkingLevel,
    ) -> Result<Pin<Box<dyn Stream<Item = StreamChunk> + Send>>> {
        let result = self
            .provider_for_request()
            .await?
            .generate_stream_with_thinking(messages, tools, thinking)
            .await;
        if self.is_oauth_provider() && result.as_ref().is_err_and(Self::is_refreshable_auth_error) {
            info!(provider = %self.provider_name, "401 received; forcing OAuth token refresh and retrying");
            return self
                .provider_after_forced_refresh()
                .await?
                .generate_stream_with_thinking(messages, tools, thinking)
                .await;
        }
        result
    }

    async fn generate_with_thinking_config(
        &self,
        messages: &[Message],
        tools: &[Tool],
        config: ThinkingConfig,
    ) -> Result<LLMResponse> {
        let result = self
            .provider_for_request()
            .await?
            .generate_with_thinking_config(messages, tools, config)
            .await;
        if self.is_oauth_provider() && result.as_ref().is_err_and(Self::is_refreshable_auth_error) {
            info!(provider = %self.provider_name, "401 received; forcing OAuth token refresh and retrying");
            return self
                .provider_after_forced_refresh()
                .await?
                .generate_with_thinking_config(messages, tools, config)
                .await;
        }
        result
    }

    async fn generate_stream_with_thinking_config(
        &self,
        messages: &[Message],
        tools: &[Tool],
        config: ThinkingConfig,
    ) -> Result<Pin<Box<dyn Stream<Item = StreamChunk> + Send>>> {
        let result = self
            .provider_for_request()
            .await?
            .generate_stream_with_thinking_config(messages, tools, config)
            .await;
        if self.is_oauth_provider() && result.as_ref().is_err_and(Self::is_refreshable_auth_error) {
            info!(provider = %self.provider_name, "401 received; forcing OAuth token refresh and retrying");
            return self
                .provider_after_forced_refresh()
                .await?
                .generate_stream_with_thinking_config(messages, tools, config)
                .await;
        }
        result
    }
}
