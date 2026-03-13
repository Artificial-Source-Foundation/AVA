use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, Copy, Serialize, Deserialize, PartialEq, Eq, Default)]
#[serde(rename_all = "kebab-case")]
pub enum RoutingMode {
    #[default]
    Off,
    Conservative,
}

#[derive(Debug, Clone, Copy, Serialize, Deserialize, PartialEq, Eq, Default)]
#[serde(rename_all = "kebab-case")]
pub enum RoutingProfile {
    #[default]
    Cheap,
    Capable,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq, Default)]
pub struct RoutingTarget {
    #[serde(default)]
    pub provider: Option<String>,
    #[serde(default)]
    pub model: Option<String>,
}

impl RoutingTarget {
    pub fn normalize(&mut self) {
        if let Some(provider) = &mut self.provider {
            *provider = provider.trim().to_lowercase();
        }
        if let Some(model) = &mut self.model {
            *model = model.trim().to_string();
        }
    }

    pub fn is_complete(&self) -> bool {
        self.provider.is_some() && self.model.is_some()
    }
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq, Default)]
pub struct RoutingTargets {
    #[serde(default)]
    pub cheap: RoutingTarget,
    #[serde(default)]
    pub capable: RoutingTarget,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
pub struct RoutingConfig {
    #[serde(default)]
    pub mode: RoutingMode,
    #[serde(default)]
    pub targets: RoutingTargets,
}

impl Default for RoutingConfig {
    fn default() -> Self {
        Self {
            mode: RoutingMode::Off,
            targets: RoutingTargets::default(),
        }
    }
}

impl RoutingConfig {
    pub fn is_enabled(&self) -> bool {
        self.mode != RoutingMode::Off
    }

    pub fn normalize(&mut self) {
        self.targets.cheap.normalize();
        self.targets.capable.normalize();
    }

    pub fn target_for(&self, profile: RoutingProfile) -> Option<&RoutingTarget> {
        let target = match profile {
            RoutingProfile::Cheap => &self.targets.cheap,
            RoutingProfile::Capable => &self.targets.capable,
        };
        target.is_complete().then_some(target)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn routing_config_normalizes_targets() {
        let mut config = RoutingConfig {
            mode: RoutingMode::Conservative,
            targets: RoutingTargets {
                cheap: RoutingTarget {
                    provider: Some(" OpenAI ".to_string()),
                    model: Some(" gpt-4o-mini ".to_string()),
                },
                capable: RoutingTarget {
                    provider: Some(" Anthropic ".to_string()),
                    model: Some(" claude-sonnet-4.6 ".to_string()),
                },
            },
        };

        config.normalize();

        assert_eq!(config.targets.cheap.provider.as_deref(), Some("openai"));
        assert_eq!(config.targets.cheap.model.as_deref(), Some("gpt-4o-mini"));
        assert_eq!(
            config.targets.capable.provider.as_deref(),
            Some("anthropic")
        );
        assert!(config.is_enabled());
    }

    #[test]
    fn routing_config_returns_only_complete_targets() {
        let config = RoutingConfig {
            mode: RoutingMode::Conservative,
            targets: RoutingTargets {
                cheap: RoutingTarget {
                    provider: Some("openai".to_string()),
                    model: Some("gpt-4o-mini".to_string()),
                },
                capable: RoutingTarget {
                    provider: Some("anthropic".to_string()),
                    model: None,
                },
            },
        };

        assert!(config.target_for(RoutingProfile::Cheap).is_some());
        assert!(config.target_for(RoutingProfile::Capable).is_none());
    }
}
