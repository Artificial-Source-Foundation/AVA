use ava_agent_orchestration::stack::AgentStack;
use ava_llm::provider::LLMProvider;
use color_eyre::eyre::Result;
use std::sync::Arc;

pub(super) async fn resolve_provider(stack: &AgentStack) -> Result<Arc<dyn LLMProvider>> {
    let (provider_name, model_name) = stack.current_model().await;
    let provider = stack
        .router
        .route_required(&provider_name, &model_name)
        .await?;
    Ok(provider)
}
