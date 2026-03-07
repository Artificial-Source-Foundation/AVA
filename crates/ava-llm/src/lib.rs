pub mod credential_test;
pub mod pool;
pub mod provider;
pub mod providers;
pub mod retry;
pub mod router;

pub use credential_test::{default_model_for_provider, test_provider_credentials};
pub use pool::ConnectionPool;
pub use provider::LLMProvider;
pub use router::{ModelRouter, ProviderFactory};

pub fn healthcheck() -> bool {
    true
}
