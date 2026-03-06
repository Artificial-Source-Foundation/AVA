use std::collections::HashMap;
use std::sync::Arc;

use ava_config::CredentialStore;
use ava_types::{AvaError, Result};
use tokio::sync::RwLock;

use crate::provider::LLMProvider;
use crate::providers::create_provider;

pub struct ModelRouter {
    credentials: Arc<RwLock<CredentialStore>>,
    providers: RwLock<HashMap<String, Arc<dyn LLMProvider>>>,
}

impl ModelRouter {
    pub fn new(credentials: CredentialStore) -> Self {
        Self {
            credentials: Arc::new(RwLock::new(credentials)),
            providers: RwLock::new(HashMap::new()),
        }
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

        let credentials = self.credentials.read().await.clone();
        let created = create_provider(provider, model, &credentials)?;
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

    pub async fn route_required(&self, provider: &str, model: &str) -> Result<Arc<dyn LLMProvider>> {
        self.route(provider, model).await.map_err(|error| {
            AvaError::ConfigError(format!(
                "Could not route provider {provider} with model {model}: {error}"
            ))
        })
    }
}
