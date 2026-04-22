//! Backend helpers layered over shared session contracts.

use ava_types::{Session, ThinkingLevel};

use crate::run_context::AgentRunContext;

pub fn run_context_from_session(session: &Session) -> AgentRunContext {
    let mut context = AgentRunContext::default();
    let metadata = session.metadata.get("runContext");

    context.provider = metadata
        .and_then(|value| value.get("provider"))
        .and_then(serde_json::Value::as_str)
        .map(str::to_string)
        .or_else(|| {
            session
                .metadata
                .get("routing")
                .and_then(|value| value.get("provider"))
                .and_then(serde_json::Value::as_str)
                .map(str::to_string)
        });
    context.model = metadata
        .and_then(|value| value.get("model"))
        .and_then(serde_json::Value::as_str)
        .map(str::to_string)
        .or_else(|| {
            session
                .metadata
                .get("routing")
                .and_then(|value| value.get("model"))
                .and_then(serde_json::Value::as_str)
                .map(str::to_string)
        });
    context.thinking_level = metadata
        .and_then(|value| value.get("thinkingLevel"))
        .and_then(serde_json::Value::as_str)
        .map(parse_persisted_thinking_level);
    context.auto_compact = metadata
        .and_then(|value| value.get("autoCompact"))
        .and_then(serde_json::Value::as_bool);
    context.compaction_threshold = metadata
        .and_then(|value| value.get("compactionThreshold"))
        .and_then(serde_json::Value::as_u64)
        .map(|value| value as u8);
    context.compaction_provider = metadata
        .and_then(|value| value.get("compactionProvider"))
        .and_then(serde_json::Value::as_str)
        .map(str::to_string);
    context.compaction_model = metadata
        .and_then(|value| value.get("compactionModel"))
        .and_then(serde_json::Value::as_str)
        .map(str::to_string);

    context
}

fn parse_persisted_thinking_level(level: &str) -> ThinkingLevel {
    match level {
        "off" => ThinkingLevel::Off,
        "low" => ThinkingLevel::Low,
        "medium" => ThinkingLevel::Medium,
        "high" => ThinkingLevel::High,
        "max" | "xhigh" => ThinkingLevel::Max,
        _ => ThinkingLevel::Off,
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    #[test]
    fn run_context_from_session_recovers_effective_run_settings() {
        let session = Session::new().with_metadata(json!({
            "runContext": {
                "provider": "openai",
                "model": "gpt-5.4",
                "thinkingLevel": "high",
                "autoCompact": true,
                "compactionThreshold": 72,
                "compactionProvider": "anthropic",
                "compactionModel": "claude-sonnet-4.6"
            }
        }));

        let context = run_context_from_session(&session);

        assert_eq!(context.provider.as_deref(), Some("openai"));
        assert_eq!(context.model.as_deref(), Some("gpt-5.4"));
        assert_eq!(context.thinking_level, Some(ThinkingLevel::High));
        assert_eq!(context.auto_compact, Some(true));
        assert_eq!(context.compaction_threshold, Some(72));
        assert_eq!(context.compaction_provider.as_deref(), Some("anthropic"));
        assert_eq!(
            context.compaction_model.as_deref(),
            Some("claude-sonnet-4.6")
        );
    }

    #[test]
    fn run_context_from_session_falls_back_to_routing_identity() {
        let session = Session::new().with_metadata(json!({
            "routing": {
                "provider": "openai",
                "model": "gpt-5.4-nano"
            }
        }));

        let context = run_context_from_session(&session);

        assert_eq!(context.provider.as_deref(), Some("openai"));
        assert_eq!(context.model.as_deref(), Some("gpt-5.4-nano"));
        assert_eq!(context.thinking_level, None);
        assert_eq!(context.auto_compact, None);
        assert_eq!(context.compaction_threshold, None);
        assert_eq!(context.compaction_provider, None);
        assert_eq!(context.compaction_model, None);
    }
}
