use super::*;

impl App {
    /// Fire hooks asynchronously. Results are sent back via AppEvent::HookResult.
    /// If no hooks are registered for the event, this is a no-op.
    pub(crate) fn fire_hooks_async(
        &self,
        event: HookEvent,
        context: HookContext,
        app_tx: mpsc::UnboundedSender<AppEvent>,
    ) {
        // Skip if no hooks are registered (avoids spawning for the common case)
        if self.state.hooks.is_empty() {
            return;
        }
        let registry = self.state.hooks.clone();
        // Guard against missing Tokio runtime (e.g., in sync tests)
        if tokio::runtime::Handle::try_current().is_err() {
            return;
        }
        tokio::spawn(async move {
            let (_, executions) = HookRunner::run_hooks(&registry, event.clone(), context).await;
            for exec in &executions {
                let _ = app_tx.send(AppEvent::HookResult {
                    event: exec.event.clone(),
                    result: exec.result.clone(),
                    description: exec.description.clone(),
                });
            }
        });
    }

    /// Build a HookContext with common session/model fields populated.
    pub(crate) fn build_hook_context(&self, event: &HookEvent) -> HookContext {
        let mut ctx = HookContext::for_event(event);
        ctx.model = Some(self.state.agent.model_name.clone());
        ctx.session_id = self
            .state
            .session
            .current_session
            .as_ref()
            .map(|s| s.id.to_string());
        ctx.tokens_used = Some(
            self.state.agent.tokens_used.cumulative_input
                + self.state.agent.tokens_used.cumulative_output,
        );
        ctx.cost_usd = Some(self.state.agent.cost);
        ctx
    }
}
