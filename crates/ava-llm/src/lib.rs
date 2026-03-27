//! AVA LLM — unified interface for LLM providers with routing and circuit breaking.
//!
//! This crate provides:
//! - Provider abstraction for multiple LLM services
//! - Connection pooling and retry logic
//! - Circuit breaker pattern for resilience
//! - Model availability tracking with fallback chains

pub mod circuit_breaker;
pub mod credential_test;
mod dynamic_provider;
pub mod message_transform;
pub mod pool;
pub mod provider;
pub mod providers;
pub mod retry;
pub mod router;
pub mod thinking;
pub mod usage;

pub use credential_test::{default_model_for_provider, test_provider_credentials};
pub use message_transform::{normalize_messages, ProviderKind};
pub use pool::ConnectionPool;
pub use provider::{LLMProvider, NormalizingProvider, ProviderCapabilities, ProviderErrorKind};
pub use router::{ModelRouter, ProviderFactory, RouteDecision, RouteRequirements, RouteSource};
pub use thinking::{
    ResolvedThinkingConfig, ThinkingBudgetFallback, ThinkingBudgetSupport, ThinkingConfig,
};
