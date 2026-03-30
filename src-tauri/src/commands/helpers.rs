//! Shared helper functions for agent and hq commands.

/// Parse a domain string from the frontend into an HQ [`Domain`].
pub fn parse_domain(s: &str) -> Option<ava_hq::Domain> {
    match s.to_lowercase().as_str() {
        "frontend" => Some(ava_hq::Domain::Frontend),
        "backend" => Some(ava_hq::Domain::Backend),
        "qa" => Some(ava_hq::Domain::QA),
        "research" => Some(ava_hq::Domain::Research),
        "debug" => Some(ava_hq::Domain::Debug),
        "fullstack" => Some(ava_hq::Domain::Fullstack),
        "devops" => Some(ava_hq::Domain::DevOps),
        _ => None,
    }
}

/// Resolve a model spec string (e.g. "openrouter/anthropic/claude-haiku-4.5")
/// into a provider Arc via the stack's router. Returns `None` if the spec is empty
/// or resolution fails.
pub async fn resolve_model_spec(
    stack: &ava_agent::stack::AgentStack,
    model_spec: &str,
) -> Option<std::sync::Arc<dyn ava_llm::provider::LLMProvider>> {
    if model_spec.is_empty() {
        return None;
    }
    // Use the same parsing logic as AgentStack: first segment before '/' is
    // the provider, the rest is the model name. Bare names go through the
    // model registry.
    let (provider_name, model_name) = if let Some(idx) = model_spec.find('/') {
        let prov = &model_spec[..idx];
        let mdl = &model_spec[idx + 1..];
        (prov.to_string(), mdl.to_string())
    } else {
        // Bare model name — try the registry, fall back to the current provider
        let (cur_prov, _) = stack.current_model().await;
        (cur_prov, model_spec.to_string())
    };
    stack
        .router
        .route_required(&provider_name, &model_name)
        .await
        .ok()
}

/// Collect conversation history up to (but not including) the last user message.
pub fn collect_history_before_last_user(
    messages: &[ava_types::Message],
) -> Vec<ava_types::Message> {
    let last_user_pos = messages
        .iter()
        .rposition(|m| m.role == ava_types::Role::User);
    match last_user_pos {
        Some(pos) => messages[..pos].to_vec(),
        None => vec![],
    }
}
