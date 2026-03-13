use std::collections::HashMap;
use std::path::PathBuf;
use std::sync::Arc;

use ava_config::CredentialStore;
use ava_types::{AvaError, Result};
use tokio::sync::{Mutex, RwLock};

use crate::dynamic_provider::{resolve_credentials_snapshot, DynamicCredentialProvider};
use crate::pool::ConnectionPool;
use crate::provider::LLMProvider;
use crate::providers::create_provider;

/// External provider factory for providers that live outside ava-llm
/// (e.g., CLI agent providers in ava-cli-providers).
pub trait ProviderFactory: Send + Sync {
    fn create(&self, provider_name: &str, model: &str) -> Result<Box<dyn LLMProvider>>;
    fn handles(&self, provider_name: &str) -> bool;
}

pub struct ModelRouter {
    credentials: Arc<RwLock<CredentialStore>>,
    providers: RwLock<HashMap<String, Arc<dyn LLMProvider>>>,
    factories: Vec<Arc<dyn ProviderFactory>>,
    pool: Arc<ConnectionPool>,
    credentials_path: Option<PathBuf>,
    refresh_locks: Arc<Mutex<HashMap<String, Arc<Mutex<()>>>>>,
}

impl ModelRouter {
    pub fn new(credentials: CredentialStore) -> Self {
        Self::with_pool_and_credentials_path(
            credentials,
            Arc::new(ConnectionPool::new()),
            CredentialStore::default_path().ok(),
        )
    }

    pub fn with_pool(credentials: CredentialStore, pool: Arc<ConnectionPool>) -> Self {
        Self::with_pool_and_credentials_path(
            credentials,
            pool,
            CredentialStore::default_path().ok(),
        )
    }

    pub fn with_pool_and_credentials_path(
        credentials: CredentialStore,
        pool: Arc<ConnectionPool>,
        credentials_path: Option<PathBuf>,
    ) -> Self {
        Self {
            credentials: Arc::new(RwLock::new(credentials)),
            providers: RwLock::new(HashMap::new()),
            factories: Vec::new(),
            pool,
            credentials_path,
            refresh_locks: Arc::new(Mutex::new(HashMap::new())),
        }
    }

    /// Register an external provider factory (e.g., for CLI agent providers).
    pub fn register_factory(&mut self, factory: Arc<dyn ProviderFactory>) {
        self.factories.push(factory);
    }

    pub fn pool(&self) -> &Arc<ConnectionPool> {
        &self.pool
    }

    pub async fn update_credentials(&self, credentials: CredentialStore) {
        {
            let mut guard = self.credentials.write().await;
            *guard = credentials;
        }

        let mut providers = self.providers.write().await;
        providers.clear();
    }

    pub async fn route(&self, provider: &str, model: &str) -> Result<Arc<dyn LLMProvider>> {
        let cache_key = format!("{provider}:{model}");

        if let Some(cached) = self.providers.read().await.get(&cache_key).cloned() {
            return Ok(cached);
        }

        // Try external factories first
        let created: Box<dyn LLMProvider> = if let Some(factory) =
            self.factories.iter().find(|f| f.handles(provider))
        {
            factory.create(provider, model)?
        } else {
            let snapshot = resolve_credentials_snapshot(
                &self.credentials,
                provider,
                self.credentials_path.as_deref(),
                &self.refresh_locks,
            )
            .await?;
            let metadata_provider = create_provider(provider, model, &snapshot, self.pool.clone())?;

            Box::new(DynamicCredentialProvider::new(
                provider,
                model,
                self.pool.clone(),
                self.credentials.clone(),
                self.credentials_path.clone(),
                self.refresh_locks.clone(),
                metadata_provider.as_ref(),
            ))
        };

        let created: Arc<dyn LLMProvider> = Arc::from(created);

        let mut providers = self.providers.write().await;
        if let Some(existing) = providers.get(&cache_key) {
            return Ok(existing.clone());
        }
        providers.insert(cache_key, created.clone());

        Ok(created)
    }

    pub async fn available_providers(&self) -> Vec<String> {
        self.credentials
            .read()
            .await
            .configured_providers()
            .into_iter()
            .map(ToString::to_string)
            .collect()
    }

    pub async fn cache_size(&self) -> usize {
        self.providers.read().await.len()
    }

    pub async fn route_required(
        &self,
        provider: &str,
        model: &str,
    ) -> Result<Arc<dyn LLMProvider>> {
        self.route(provider, model).await.map_err(|error| {
            AvaError::ConfigError(format!(
                "Could not route provider {provider} with model {model}: {error}"
            ))
        })
    }
}
