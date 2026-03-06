pub mod credential_test;
pub mod provider;
pub mod providers;
pub mod router;

pub use credential_test::{default_model_for_provider, test_provider_credentials};
pub use provider::LLMProvider;
pub use router::{ModelRouter, ProviderFactory};

pub fn healthcheck() -> bool {
    true
}
