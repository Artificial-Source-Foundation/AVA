pub mod provider;
pub mod providers;
pub mod router;

pub use provider::LLMProvider;
pub use router::{ModelRouter, RoutingTaskType};

pub fn healthcheck() -> bool {
    true
}
