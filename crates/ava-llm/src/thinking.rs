use ava_types::ThinkingLevel;

#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub struct ThinkingConfig {
    pub level: ThinkingLevel,
    pub budget_tokens: Option<u32>,
}

impl ThinkingConfig {
    pub const fn disabled() -> Self {
        Self {
            level: ThinkingLevel::Off,
            budget_tokens: None,
        }
    }

    pub const fn new(level: ThinkingLevel, budget_tokens: Option<u32>) -> Self {
        Self {
            level,
            budget_tokens,
        }
    }

    pub fn is_enabled(self) -> bool {
        self.level != ThinkingLevel::Off
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ThinkingBudgetFallback {
    Unsupported,
    Ignored,
    Clamped { requested: u32, applied: u32 },
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ThinkingBudgetSupport {
    None,
    Qualitative,
    Quantitative,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct ResolvedThinkingConfig {
    pub requested: ThinkingConfig,
    pub applied: ThinkingConfig,
    pub budget_support: ThinkingBudgetSupport,
    pub fallback: Option<ThinkingBudgetFallback>,
}

impl ResolvedThinkingConfig {
    pub const fn disabled() -> Self {
        Self {
            requested: ThinkingConfig::disabled(),
            applied: ThinkingConfig::disabled(),
            budget_support: ThinkingBudgetSupport::None,
            fallback: None,
        }
    }

    pub const fn unsupported(requested: ThinkingConfig) -> Self {
        Self {
            requested,
            applied: ThinkingConfig {
                level: requested.level,
                budget_tokens: None,
            },
            budget_support: ThinkingBudgetSupport::None,
            fallback: Some(ThinkingBudgetFallback::Unsupported),
        }
    }

    pub const fn qualitative(
        requested: ThinkingConfig,
        fallback: Option<ThinkingBudgetFallback>,
    ) -> Self {
        Self {
            requested,
            applied: ThinkingConfig {
                level: requested.level,
                budget_tokens: None,
            },
            budget_support: ThinkingBudgetSupport::Qualitative,
            fallback,
        }
    }

    pub const fn quantitative(
        applied: ThinkingConfig,
        fallback: Option<ThinkingBudgetFallback>,
    ) -> Self {
        Self {
            requested: applied,
            applied,
            budget_support: ThinkingBudgetSupport::Quantitative,
            fallback,
        }
    }

    pub const fn quantitative_from(
        requested: ThinkingConfig,
        applied_budget_tokens: u32,
        fallback: Option<ThinkingBudgetFallback>,
    ) -> Self {
        Self {
            requested,
            applied: ThinkingConfig {
                level: requested.level,
                budget_tokens: Some(applied_budget_tokens),
            },
            budget_support: ThinkingBudgetSupport::Quantitative,
            fallback,
        }
    }
}
