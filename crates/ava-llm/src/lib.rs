//! AVA LLM — unified interface for LLM providers with routing and circuit breaking.
//!
//! This crate provides:
//! - Provider abstraction for multiple LLM services
//! - Connection pooling and retry logic
//! - Circuit breaker pattern for resilience
//! - Model availability tracking with fallback chains

pub mod availability;
pub mod circuit_breaker;
pub mod credential_test;
mod dynamic_provider;
pub mod lead_worker;
pub mod message_transform;
pub mod model_classifier;
pub mod pool;
pub mod provider;
pub mod providers;
pub mod retry;
pub mod router;
pub mod thinking;

pub use availability::{FallbackChain, ModelAvailability, ModelStatus};
pub use credential_test::{default_model_for_provider, test_provider_credentials};
pub use lead_worker::LeadWorkerProvider;
pub use message_transform::{normalize_messages, ProviderKind};
pub use model_classifier::{
    ClassifierRouter, ComplexityClassifier, ComplexityTier, FallbackStrategy, OverrideStrategy,
    RoutingContext, RoutingDecision, RoutingStrategy,
};
pub use pool::ConnectionPool;
pub use provider::{LLMProvider, NormalizingProvider, ProviderCapabilities, ProviderErrorKind};
pub use router::{ModelRouter, ProviderFactory, RouteDecision, RouteRequirements, RouteSource};
pub use thinking::{
    ResolvedThinkingConfig, ThinkingBudgetFallback, ThinkingBudgetSupport, ThinkingConfig,
};
