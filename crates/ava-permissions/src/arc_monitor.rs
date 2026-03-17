//! ARC Monitor integration — evaluate actions against safety heuristics.
//!
//! Provides the interface for an external ARC Monitor service with a
//! heuristic fallback when no remote endpoint is available.

/// Outcome of an ARC Monitor evaluation.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum ArcOutcome {
    /// Action is safe to proceed.
    Ok,
    /// Action needs user confirmation, with a reason message.
    Ask(String),
    /// Action should be steered — the monitor suggests a different approach.
    Steer(String),
}

/// Client for evaluating actions against an ARC Monitor endpoint.
///
/// Currently uses heuristic fallback (no HTTP calls). The interface is
/// designed so a real HTTP backend can be swapped in later.
#[derive(Debug, Clone)]
pub struct ArcMonitorClient {
    /// Base URL for the ARC Monitor API (reserved for future use).
    #[allow(dead_code)]
    endpoint: Option<String>,
}

impl ArcMonitorClient {
    /// Create a new client with no remote endpoint (heuristic-only mode).
    pub fn new() -> Self {
        Self { endpoint: None }
    }

    /// Create a new client pointing at a remote ARC Monitor endpoint.
    #[allow(dead_code)]
    pub fn with_endpoint(url: impl Into<String>) -> Self {
        Self {
            endpoint: Some(url.into()),
        }
    }

    /// Evaluate an action with the given context string.
    ///
    /// Uses heuristic rules to classify the action. Dangerous keywords
    /// trigger `Ask` or `Steer`; everything else is `Ok`.
    pub fn evaluate(&self, action: &str, context: &str) -> ArcOutcome {
        let combined = format!("{} {}", action, context).to_lowercase();

        // Critical actions that should be steered away from
        if combined.contains("rm -rf /")
            || combined.contains("sudo rm")
            || combined.contains("format disk")
            || combined.contains("drop database")
        {
            return ArcOutcome::Steer(
                "This action is potentially destructive and should be reconsidered".to_string(),
            );
        }

        // Actions that need confirmation
        if combined.contains("delete")
            || combined.contains("remove")
            || combined.contains("overwrite")
            || combined.contains("force push")
        {
            return ArcOutcome::Ask(format!(
                "Action '{}' may have side effects — please confirm",
                action
            ));
        }

        ArcOutcome::Ok
    }
}

impl Default for ArcMonitorClient {
    fn default() -> Self {
        Self::new()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn safe_action_returns_ok() {
        let client = ArcMonitorClient::new();
        assert_eq!(client.evaluate("read", "file.txt"), ArcOutcome::Ok);
    }

    #[test]
    fn destructive_action_returns_steer() {
        let client = ArcMonitorClient::new();
        let outcome = client.evaluate("bash", "rm -rf / --no-preserve-root");
        assert!(matches!(outcome, ArcOutcome::Steer(_)));
    }

    #[test]
    fn delete_action_returns_ask() {
        let client = ArcMonitorClient::new();
        let outcome = client.evaluate("delete", "old_backup");
        assert!(matches!(outcome, ArcOutcome::Ask(_)));
    }
}
