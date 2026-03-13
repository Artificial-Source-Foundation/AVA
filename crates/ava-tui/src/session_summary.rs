use ava_types::Session;

#[derive(Debug, Clone, Copy, PartialEq)]
pub struct SessionCostSummary {
    pub total_usd: f64,
    pub budget_usd: Option<f64>,
    pub input_tokens: usize,
    pub output_tokens: usize,
    pub last_alert_threshold_percent: Option<u8>,
}

pub fn cost_summary(session: &Session) -> Option<SessionCostSummary> {
    let summary = session.metadata.get("costSummary")?.as_object()?;
    Some(SessionCostSummary {
        total_usd: summary.get("totalUsd")?.as_f64()?,
        budget_usd: summary.get("budgetUsd").and_then(|value| value.as_f64()),
        input_tokens: summary
            .get("inputTokens")
            .and_then(|value| value.as_u64())
            .and_then(|value| usize::try_from(value).ok())
            .unwrap_or_default(),
        output_tokens: summary
            .get("outputTokens")
            .and_then(|value| value.as_u64())
            .and_then(|value| usize::try_from(value).ok())
            .unwrap_or_default(),
        last_alert_threshold_percent: summary
            .get("lastAlertThresholdPercent")
            .and_then(|value| value.as_u64())
            .and_then(|value| u8::try_from(value).ok()),
    })
}

pub fn route_summary(session: &Session) -> Option<String> {
    let routing = session.metadata.get("routing")?.as_object()?;
    let profile = routing.get("profile").and_then(|value| value.as_str())?;
    let provider = routing.get("provider").and_then(|value| value.as_str());
    let model = routing
        .get("displayModel")
        .or_else(|| routing.get("model"))
        .and_then(|value| value.as_str());
    let source = routing.get("source").and_then(|value| value.as_str());

    let mut summary = format!("{profile} route");
    if let (Some(provider), Some(model)) = (provider, model) {
        summary.push_str(&format!(" via {provider}/{model}"));
    }
    if let Some(source) = source {
        summary.push_str(&format!(" ({source})"));
    }
    Some(summary)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_cost_summary_fields() {
        let session = Session::new().with_metadata(serde_json::json!({
            "costSummary": {
                "totalUsd": 0.42,
                "budgetUsd": 1.0,
                "inputTokens": 1200,
                "outputTokens": 340,
                "lastAlertThresholdPercent": 75
            }
        }));

        let summary = cost_summary(&session).expect("cost summary should parse");
        assert_eq!(summary.input_tokens, 1200);
        assert_eq!(summary.output_tokens, 340);
        assert_eq!(summary.budget_usd, Some(1.0));
        assert_eq!(summary.last_alert_threshold_percent, Some(75));
    }

    #[test]
    fn parses_route_summary_fields() {
        let session = Session::new().with_metadata(serde_json::json!({
            "routing": {
                "profile": "cheap",
                "provider": "openai",
                "displayModel": "gpt-4o-mini",
                "source": "configured"
            }
        }));

        assert_eq!(
            route_summary(&session).as_deref(),
            Some("cheap route via openai/gpt-4o-mini (configured)")
        );
    }
}
