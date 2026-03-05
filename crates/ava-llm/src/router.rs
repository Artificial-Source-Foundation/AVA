use std::collections::HashMap;

use ava_types::{AvaError, Result};
use serde::{Deserialize, Serialize};

use crate::provider::LLMProvider;

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
pub enum RoutingTaskType {
    Planning,
    CodeGeneration,
    Testing,
    Review,
    Research,
    Debug,
    Simple,
}

pub struct ModelRouter {
    providers: HashMap<String, Box<dyn LLMProvider>>,
    default: String,
}

impl ModelRouter {
    const TIER_STRONGEST: &'static str = "strongest";
    const TIER_MID: &'static str = "mid";
    const TIER_CHEAP: &'static str = "cheap";

    pub fn new(default: impl Into<String>) -> Self {
        Self {
            providers: HashMap::new(),
            default: default.into(),
        }
    }

    pub fn register(&mut self, name: impl Into<String>, provider: Box<dyn LLMProvider>) {
        self.providers.insert(name.into(), provider);
    }

    pub fn get(&self, name: &str) -> Option<&dyn LLMProvider> {
        self.providers.get(name).map(|provider| provider.as_ref())
    }

    pub fn route(&self, task: RoutingTaskType) -> Result<&dyn LLMProvider> {
        let preferred = match task {
            RoutingTaskType::Planning | RoutingTaskType::Research | RoutingTaskType::Debug => {
                Self::TIER_STRONGEST
            }
            RoutingTaskType::CodeGeneration
            | RoutingTaskType::Testing
            | RoutingTaskType::Review => Self::TIER_MID,
            RoutingTaskType::Simple => Self::TIER_CHEAP,
        };

        self.get(preferred)
            .or_else(|| self.get(&self.default))
            .ok_or_else(|| AvaError::NotFound("no provider registered for routing".to_string()))
    }
}
