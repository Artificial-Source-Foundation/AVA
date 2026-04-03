use std::cmp::Ordering;
use std::collections::HashMap;
use std::path::PathBuf;
use std::sync::Arc;

use ava_config::{
    fallback_catalog,
    model_catalog::registry::{registry, RegisteredModel},
    CredentialStore, RoutingConfig, RoutingProfile, RoutingTarget,
};
use ava_plugin::PluginManager;
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

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum RouteSource {
    ConfigDefault,
    ManualOverride,
    PolicyTarget,
    PolicyAuto,
    Fallback,
}

impl RouteSource {
    pub fn as_str(self) -> &'static str {
        match self {
            Self::ConfigDefault => "config-default",
            Self::ManualOverride => "manual-override",
            Self::PolicyTarget => "policy-target",
            Self::PolicyAuto => "policy-auto",
            Self::Fallback => "fallback",
        }
    }
}

#[derive(Debug, Clone, Copy, Default)]
pub struct RouteRequirements {
    pub needs_vision: bool,
    pub prefer_reasoning: bool,
}

#[derive(Debug, Clone)]
pub struct RouteDecision {
    pub provider: String,
    pub model: String,
    pub display_model: String,
    pub profile: RoutingProfile,
    pub source: RouteSource,
    pub reasons: Vec<String>,
    pub cost_input_per_million: Option<f64>,
    pub cost_output_per_million: Option<f64>,
}

impl RouteDecision {
    pub fn fixed(
        provider: impl Into<String>,
        model: impl Into<String>,
        profile: RoutingProfile,
        source: RouteSource,
        reasons: Vec<String>,
    ) -> Self {
        let provider = provider.into();
        let model = model.into();
        let display_model = normalize_display_model(&provider, &model);
        let pricing = registry()
            .find_for_provider(&registry_provider(&provider), &display_model)
            .map(|entry| (entry.cost.input_per_million, entry.cost.output_per_million));
        Self {
            provider,
            model,
            display_model,
            profile,
            source,
            reasons,
            cost_input_per_million: pricing.map(|(input, _)| input),
            cost_output_per_million: pricing.map(|(_, output)| output),
        }
    }

    pub fn summary(&self) -> String {
        format!(
            "routing: {} -> {}/{} [{}]",
            match self.profile {
                RoutingProfile::Cheap => "cheap",
                RoutingProfile::Capable => "capable",
            },
            self.provider,
            self.display_model,
            self.source.as_str(),
        )
    }
}

#[derive(Debug, Clone)]
struct RouteCandidate {
    provider: String,
    api_model: String,
    display_model: String,
    cost_input_per_million: f64,
    cost_output_per_million: f64,
    capability_score: i32,
}

pub struct ModelRouter {
    credentials: Arc<RwLock<CredentialStore>>,
    providers: RwLock<HashMap<String, Arc<dyn LLMProvider>>>,
    factories: RwLock<Vec<Arc<dyn ProviderFactory>>>,
    pool: Arc<ConnectionPool>,
    credentials_path: Option<PathBuf>,
    refresh_locks: Arc<Mutex<HashMap<String, Arc<Mutex<()>>>>>,
    /// Optional plugin manager injected by the AgentStack for `request.headers` hook.
    plugin_manager: Option<Arc<tokio::sync::Mutex<PluginManager>>>,
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
            factories: RwLock::new(Vec::new()),
            pool,
            credentials_path,
            refresh_locks: Arc::new(Mutex::new(HashMap::new())),
            plugin_manager: None,
        }
    }

    /// Attach a plugin manager so that the `request.headers` hook is called
    /// before every outgoing LLM API request.
    pub fn set_plugin_manager(&mut self, pm: Arc<tokio::sync::Mutex<PluginManager>>) {
        self.plugin_manager = Some(pm);
    }

    /// Register an external provider factory (e.g., for CLI agent providers).
    pub fn register_factory(&mut self, factory: Arc<dyn ProviderFactory>) {
        self.factories.get_mut().push(factory);
    }

    /// Register an external provider factory from a shared reference (for late/background registration).
    pub async fn register_factory_async(&self, factory: Arc<dyn ProviderFactory>) {
        self.factories.write().await.push(factory);
    }

    pub fn pool(&self) -> &Arc<ConnectionPool> {
        &self.pool
    }

    pub async fn update_credentials(&self, credentials: CredentialStore) {
        {
            let mut guard = self.credentials.write().await;
            *guard = credentials;
        }

        // B63/B47 seam: provider instances cache auth/base-URL state and must be
        // dropped on credential refresh. Route decisions are recomputed per run,
        // so there is no separate routing-decision cache to invalidate here.
        self.invalidate_provider_cache().await;
    }

    /// Update the API key for a single provider and invalidate the provider cache.
    ///
    /// Used by the plugin auth sub-protocol to store credentials returned by
    /// `authorize_with_plugin` without replacing the entire credential store.
    pub async fn update_credentials_for_provider(&self, provider: &str, api_key: String) {
        {
            let mut guard = self.credentials.write().await;
            let entry = guard
                .providers
                .entry(provider.to_string())
                .or_insert_with(|| ava_config::ProviderCredential {
                    api_key: String::new(),
                    base_url: None,
                    org_id: None,
                    oauth_token: None,
                    oauth_refresh_token: None,
                    oauth_expires_at: None,
                    oauth_account_id: None,
                    litellm_compatible: None,
                    loop_prone: None,
                });
            entry.api_key = api_key;
        }
        self.invalidate_provider_cache().await;
    }

    async fn invalidate_provider_cache(&self) {
        let mut providers = self.providers.write().await;
        providers.clear();
    }

    pub async fn route(&self, provider: &str, model: &str) -> Result<Arc<dyn LLMProvider>> {
        let cache_key = format!("{provider}:{model}");

        if let Some(cached) = self.providers.read().await.get(&cache_key).cloned() {
            return Ok(cached);
        }

        // Try external factories first
        let factory_result = {
            let factories = self.factories.read().await;
            factories
                .iter()
                .find(|f| f.handles(provider))
                .map(|f| f.create(provider, model))
        };
        let created: Box<dyn LLMProvider> = if let Some(result) = factory_result {
            result?
        } else {
            let snapshot = resolve_credentials_snapshot(
                &self.credentials,
                provider,
                self.credentials_path.as_deref(),
                &self.refresh_locks,
            )
            .await?;
            let metadata_provider = create_provider(provider, model, &snapshot, self.pool.clone())?;

            let dyn_provider = DynamicCredentialProvider::new(
                provider,
                model,
                self.pool.clone(),
                self.credentials.clone(),
                self.credentials_path.clone(),
                self.refresh_locks.clone(),
                metadata_provider.as_ref(),
            );
            // Attach plugin manager if available so request.headers hook fires.
            let dyn_provider = if let Some(pm) = &self.plugin_manager {
                dyn_provider.with_plugin_manager(pm.clone())
            } else {
                dyn_provider
            };
            Box::new(dyn_provider)
        };

        let created: Arc<dyn LLMProvider> = Arc::from(created);

        let mut providers = self.providers.write().await;
        if let Some(existing) = providers.get(&cache_key) {
            return Ok(existing.clone());
        }
        providers.insert(cache_key, created.clone());

        Ok(created)
    }

    pub async fn decide_route(
        &self,
        requested_provider: &str,
        requested_model: &str,
        routing: &RoutingConfig,
        profile: RoutingProfile,
        requirements: RouteRequirements,
    ) -> RouteDecision {
        let available_providers = self.available_providers().await;
        let mut reasons = Vec::new();

        if let Some(target) = routing.target_for(profile) {
            if let Some(target_provider) = target.provider.as_deref() {
                if available_providers
                    .iter()
                    .any(|provider| provider.as_str() == target_provider)
                {
                    reasons.push(format!(
                        "using configured {} routing target",
                        match profile {
                            RoutingProfile::Cheap => "cheap",
                            RoutingProfile::Capable => "capable",
                        }
                    ));
                    return Self::decision_from_target(
                        target,
                        requested_provider,
                        requested_model,
                        profile,
                        RouteSource::PolicyTarget,
                        reasons,
                    );
                }
                reasons.push(format!(
                    "configured {} target provider is unavailable",
                    match profile {
                        RoutingProfile::Cheap => "cheap",
                        RoutingProfile::Capable => "capable",
                    }
                ));
            } else {
                reasons.push(
                    "configured routing target is missing a provider; ignoring it".to_string(),
                );
            }
        }

        if !routing.is_enabled() {
            reasons.push("routing disabled; keeping configured model".to_string());
            return RouteDecision::fixed(
                requested_provider,
                requested_model,
                profile,
                RouteSource::ConfigDefault,
                reasons,
            );
        }

        let candidates = build_candidates(&available_providers, requirements);
        if profile == RoutingProfile::Cheap {
            if let Some(best) =
                select_cheap_candidate(&candidates, requested_provider, requested_model)
            {
                reasons.push("selected cheapest configured tool-capable route".to_string());
                return RouteDecision {
                    provider: best.provider.clone(),
                    model: best.api_model.clone(),
                    display_model: best.display_model.clone(),
                    profile,
                    source: RouteSource::PolicyAuto,
                    reasons,
                    cost_input_per_million: Some(best.cost_input_per_million),
                    cost_output_per_million: Some(best.cost_output_per_million),
                };
            }
            reasons.push("no cheaper configured route satisfied requirements".to_string());
        } else {
            reasons.push(
                "capable route keeps configured default unless a better explicit target exists"
                    .to_string(),
            );
            if !available_providers
                .iter()
                .any(|provider| provider.as_str() == requested_provider)
            {
                if let Some(best) = select_capable_candidate(&candidates) {
                    reasons.push(
                        "configured provider unavailable; selected best available capable route"
                            .to_string(),
                    );
                    return RouteDecision {
                        provider: best.provider.clone(),
                        model: best.api_model.clone(),
                        display_model: best.display_model.clone(),
                        profile,
                        source: RouteSource::PolicyAuto,
                        reasons,
                        cost_input_per_million: Some(best.cost_input_per_million),
                        cost_output_per_million: Some(best.cost_output_per_million),
                    };
                }
            }
        }

        RouteDecision::fixed(
            requested_provider,
            requested_model,
            profile,
            RouteSource::ConfigDefault,
            reasons,
        )
    }

    fn decision_from_target(
        target: &RoutingTarget,
        requested_provider: &str,
        requested_model: &str,
        profile: RoutingProfile,
        source: RouteSource,
        mut reasons: Vec<String>,
    ) -> RouteDecision {
        match (target.provider.as_deref(), target.model.as_deref()) {
            (Some(provider), Some(model)) => {
                RouteDecision::fixed(provider, model, profile, source, reasons)
            }
            _ => {
                reasons.push(
                    "configured routing target was incomplete; using requested model".to_string(),
                );
                RouteDecision::fixed(
                    requested_provider,
                    requested_model,
                    profile,
                    RouteSource::ConfigDefault,
                    reasons,
                )
            }
        }
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

    /// Clone the current credential store for read-only use (e.g., usage queries).
    pub async fn credentials_snapshot(&self) -> CredentialStore {
        self.credentials.read().await.clone()
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

fn build_candidates(
    available_providers: &[String],
    requirements: RouteRequirements,
) -> Vec<RouteCandidate> {
    let catalog = fallback_catalog();
    let reg = registry();
    let mut candidates = Vec::new();

    for provider in available_providers {
        for entry in registry_entries_for_provider(reg, provider) {
            if !entry.capabilities.tool_call {
                continue;
            }
            if requirements.needs_vision && !entry.capabilities.vision {
                continue;
            }

            let capability_score = capability_score(entry, requirements);
            let api_model = if provider == "openrouter" {
                format!("{}/{}", entry.provider, entry.id)
            } else {
                catalog
                    .models_for(provider)
                    .iter()
                    .find(|model| model.id == entry.id)
                    .map(|model| model.api_model_id(provider))
                    .unwrap_or_else(|| entry.id.clone())
            };

            candidates.push(RouteCandidate {
                provider: provider.clone(),
                api_model,
                display_model: entry.id.clone(),
                cost_input_per_million: entry.cost.input_per_million,
                cost_output_per_million: entry.cost.output_per_million,
                capability_score,
            });
        }
    }

    candidates
}

fn select_cheap_candidate<'a>(
    candidates: &'a [RouteCandidate],
    requested_provider: &str,
    requested_model: &str,
) -> Option<&'a RouteCandidate> {
    let requested_display = normalize_display_model(requested_provider, requested_model);
    let requested_cost = candidates
        .iter()
        .find(|candidate| {
            candidate.provider == requested_provider && candidate.display_model == requested_display
        })
        .map(blended_cost);

    let best = candidates
        .iter()
        .min_by(|left, right| compare_candidate_cost(left, right))?;
    if requested_cost.is_some_and(|cost| blended_cost(best) >= cost) {
        return None;
    }
    Some(best)
}

fn select_capable_candidate(candidates: &[RouteCandidate]) -> Option<&RouteCandidate> {
    candidates.iter().max_by(|left, right| {
        left.capability_score
            .cmp(&right.capability_score)
            .then_with(|| compare_candidate_cost(right, left))
    })
}

fn capability_score(entry: &RegisteredModel, requirements: RouteRequirements) -> i32 {
    let mut score = 0;
    if entry.capabilities.reasoning {
        score += 4;
    }
    if entry.capabilities.vision {
        score += 2;
    }
    if entry.limits.context_window >= 200_000 {
        score += 2;
    } else if entry.limits.context_window >= 128_000 {
        score += 1;
    }
    if entry.limits.max_output.unwrap_or_default() >= 32_000 {
        score += 1;
    }
    if requirements.prefer_reasoning && entry.capabilities.reasoning {
        score += 2;
    }
    score
}

fn compare_candidate_cost(left: &RouteCandidate, right: &RouteCandidate) -> Ordering {
    blended_cost(left)
        .partial_cmp(&blended_cost(right))
        .unwrap_or(Ordering::Equal)
        .then_with(|| left.provider.cmp(&right.provider))
        .then_with(|| left.display_model.cmp(&right.display_model))
}

fn blended_cost(candidate: &RouteCandidate) -> f64 {
    candidate.cost_input_per_million + candidate.cost_output_per_million
}

fn registry_provider(provider: &str) -> String {
    match provider {
        "gemini" => "google".to_string(),
        other => other.to_string(),
    }
}

fn registry_entries_for_provider<'a>(
    reg: &'a ava_config::model_catalog::registry::ModelRegistry,
    provider: &str,
) -> Vec<&'a RegisteredModel> {
    if provider == "openrouter" {
        return reg
            .models
            .iter()
            .filter(|entry| supports_openrouter(entry.provider.as_str()))
            .collect();
    }

    reg.models_for_provider(&registry_provider(provider))
}

fn supports_openrouter(provider: &str) -> bool {
    matches!(
        provider,
        "anthropic" | "openai" | "google" | "moonshotai" | "z-ai" | "qwen" | "alibaba"
    )
}

fn normalize_display_model(provider: &str, model: &str) -> String {
    if provider == "openrouter" {
        return model
            .split_once('/')
            .map(|(_, inner)| inner.to_string())
            .unwrap_or_else(|| model.to_string());
    }

    let normalized = model.trim();
    let reg = registry();
    reg.find(normalized)
        .map(|entry| entry.id.clone())
        .or_else(|| reg.normalize(normalized))
        .unwrap_or_else(|| normalized.to_string())
}

#[cfg(test)]
mod tests {
    use super::*;
    use ava_config::{ProviderCredential, RoutingMode, RoutingTargets};
    use std::sync::Mutex;

    static ENV_LOCK: Mutex<()> = Mutex::new(());

    fn lock_env() -> std::sync::MutexGuard<'static, ()> {
        ENV_LOCK
            .lock()
            .unwrap_or_else(|poisoned| poisoned.into_inner())
    }

    fn store_with(providers: &[&str]) -> CredentialStore {
        let mut store = CredentialStore::default();
        for provider in providers {
            store.set(
                provider,
                ProviderCredential {
                    api_key: format!("{provider}-key"),
                    base_url: None,
                    org_id: None,
                    oauth_token: None,
                    oauth_refresh_token: None,
                    oauth_expires_at: None,
                    oauth_account_id: None,
                    litellm_compatible: None,
                    loop_prone: None,
                },
            );
        }
        store
    }

    #[tokio::test]
    async fn router_prefers_cheapest_candidate_for_cheap_profile() {
        let _guard = lock_env();
        let prior_ava_openrouter = std::env::var("AVA_OPENROUTER_API_KEY").ok();
        let prior_openrouter = std::env::var("OPENROUTER_API_KEY").ok();
        std::env::remove_var("AVA_OPENROUTER_API_KEY");
        std::env::remove_var("OPENROUTER_API_KEY");

        let router = ModelRouter::new(store_with(&["anthropic", "openai"]));
        let decision = router
            .decide_route(
                "anthropic",
                "claude-sonnet-4.6",
                &RoutingConfig {
                    mode: RoutingMode::Conservative,
                    targets: RoutingTargets::default(),
                },
                RoutingProfile::Cheap,
                RouteRequirements::default(),
            )
            .await;

        assert_eq!(decision.source, RouteSource::PolicyAuto);
        assert_eq!(decision.provider, "openai");
        assert_eq!(decision.display_model, "gpt-5-mini");

        match prior_ava_openrouter {
            Some(value) => std::env::set_var("AVA_OPENROUTER_API_KEY", value),
            None => std::env::remove_var("AVA_OPENROUTER_API_KEY"),
        }
        match prior_openrouter {
            Some(value) => std::env::set_var("OPENROUTER_API_KEY", value),
            None => std::env::remove_var("OPENROUTER_API_KEY"),
        }
    }

    #[tokio::test]
    async fn router_respects_configured_target_for_capable_profile() {
        let router = ModelRouter::new(store_with(&["anthropic", "openai"]));
        let decision = router
            .decide_route(
                "anthropic",
                "claude-haiku-4.5",
                &RoutingConfig {
                    mode: RoutingMode::Conservative,
                    targets: RoutingTargets {
                        cheap: ava_config::RoutingTarget::default(),
                        capable: ava_config::RoutingTarget {
                            provider: Some("openai".to_string()),
                            model: Some("gpt-5.3-codex".to_string()),
                        },
                    },
                },
                RoutingProfile::Capable,
                RouteRequirements {
                    prefer_reasoning: true,
                    ..Default::default()
                },
            )
            .await;

        assert_eq!(decision.source, RouteSource::PolicyTarget);
        assert_eq!(decision.provider, "openai");
        assert_eq!(decision.display_model, "gpt-5.3-codex");
    }

    #[tokio::test]
    async fn router_keeps_default_when_routing_disabled() {
        let router = ModelRouter::new(store_with(&["anthropic", "openai"]));
        let decision = router
            .decide_route(
                "anthropic",
                "claude-sonnet-4.6",
                &RoutingConfig::default(),
                RoutingProfile::Cheap,
                RouteRequirements::default(),
            )
            .await;

        assert_eq!(decision.source, RouteSource::ConfigDefault);
        assert_eq!(decision.provider, "anthropic");
        assert_eq!(decision.display_model, "claude-sonnet-4.6");
    }

    #[tokio::test]
    async fn router_builds_openrouter_candidates_from_supported_upstreams() {
        let router = ModelRouter::new(store_with(&["openrouter"]));
        let decision = router
            .decide_route(
                "openrouter",
                "anthropic/claude-sonnet-4.6",
                &RoutingConfig {
                    mode: RoutingMode::Conservative,
                    targets: RoutingTargets::default(),
                },
                RoutingProfile::Cheap,
                RouteRequirements::default(),
            )
            .await;

        assert_eq!(decision.provider, "openrouter");
        assert_eq!(decision.source, RouteSource::PolicyAuto);
        assert!(decision.model.contains('/'));
    }

    #[test]
    fn decision_from_target_falls_back_when_target_is_incomplete() {
        let decision = ModelRouter::decision_from_target(
            &RoutingTarget {
                provider: Some("openai".to_string()),
                model: None,
            },
            "anthropic",
            "claude-sonnet-4.6",
            RoutingProfile::Capable,
            RouteSource::PolicyTarget,
            vec!["configured target selected".to_string()],
        );

        assert_eq!(decision.source, RouteSource::ConfigDefault);
        assert_eq!(decision.provider, "anthropic");
        assert!(decision
            .reasons
            .iter()
            .any(|reason| reason.contains("incomplete")));
    }
}
