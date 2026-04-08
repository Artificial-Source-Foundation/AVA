use tokio::sync::mpsc;
use tracing::{debug, warn};

use super::{AgentEvent, AgentLoop, CompactedMessagePreview};
use crate::trace::RunEventKind;
use ava_context::CompactionReport;
use ava_types::{Message, Role, TokenUsage, ToolCall};

impl AgentLoop {
    pub(super) async fn generate_turn_response_with_recovery(
        &mut self,
        event_tx: &Option<mpsc::UnboundedSender<AgentEvent>>,
    ) -> ava_types::Result<(String, Vec<ToolCall>, Option<TokenUsage>)> {
        match self.generate_turn_response(event_tx).await {
            Ok(result) => Ok(result),
            Err(error) if ava_llm::providers::common::is_context_overflow(&error) => {
                self.handle_context_overflow_recovery(&error, event_tx)
                    .await?;
                Self::emit(
                    event_tx,
                    AgentEvent::Progress("context compacted, retrying LLM call...".to_string()),
                );

                match self.generate_turn_response(event_tx).await {
                    Ok(result) => Ok(result),
                    Err(retry_error) => {
                        self.append_run_trace(RunEventKind::RunFailed {
                            error: retry_error.to_string(),
                        });
                        let err_event = AgentEvent::Error(retry_error.to_string());
                        Self::emit(event_tx, err_event.clone());
                        self.broadcast_event_to_plugins(&err_event).await;
                        Err(retry_error)
                    }
                }
            }
            Err(error) => {
                self.append_run_trace(RunEventKind::RunFailed {
                    error: error.to_string(),
                });
                let err_event = AgentEvent::Error(error.to_string());
                Self::emit(event_tx, err_event.clone());
                self.broadcast_event_to_plugins(&err_event).await;
                Err(error)
            }
        }
    }

    pub(super) async fn run_auto_compaction_phase(
        &mut self,
        session: &ava_types::Session,
        event_tx: &Option<mpsc::UnboundedSender<AgentEvent>>,
    ) {
        if !self.config.auto_compact || !self.context.should_compact() {
            return;
        }

        let pruned = self.context.try_prune();
        if pruned > 0 {
            Self::emit(
                event_tx,
                AgentEvent::Progress(format!("pruned {pruned} old tool output(s)")),
            );
        }

        if !self.context.should_compact() {
            return;
        }

        self.apply_session_compacting_hook(session).await;

        if let Err(error) = self.context.compact_async().await {
            warn!(error = %error, "background auto-compaction failed; continuing run");
            Self::emit(
                event_tx,
                AgentEvent::Progress(format!("context compaction skipped after failure: {error}")),
            );
            return;
        }

        self.emit_context_compacted_event(event_tx, true);
        self.reset_dynamic_instruction_activation();
        Self::emit(
            event_tx,
            AgentEvent::Progress("context compacted".to_string()),
        );
    }

    async fn handle_context_overflow_recovery(
        &mut self,
        error: &ava_types::AvaError,
        event_tx: &Option<mpsc::UnboundedSender<AgentEvent>>,
    ) -> ava_types::Result<()> {
        let token_gap = ava_llm::providers::common::parse_token_gap(&error.to_string());
        if let Some(ref gap) = token_gap {
            debug!(
                actual = gap.actual_tokens,
                max = gap.max_tokens,
                gap = gap.gap,
                gap_ratio = format!("{:.1}%", gap.gap_ratio() * 100.0),
                "parsed token gap from overflow error"
            );
        }
        warn!(
            error = %error,
            tokens = self.context.token_count(),
            token_gap = ?token_gap,
            "context overflow from provider, attempting auto-compaction"
        );
        Self::emit(
            event_tx,
            AgentEvent::Progress("context overflow detected, compacting...".to_string()),
        );

        let pruned = self.context.try_prune();
        if pruned > 0 {
            Self::emit(
                event_tx,
                AgentEvent::Progress(format!("pruned {pruned} old tool output(s)")),
            );
        }

        if let Err(compact_err) = self.context.compact_async().await {
            warn!(error = %compact_err, "compaction failed during overflow recovery");
            let err_event = AgentEvent::Error(error.to_string());
            Self::emit(event_tx, err_event.clone());
            self.broadcast_event_to_plugins(&err_event).await;
            return Err(error.clone());
        }

        self.emit_context_compacted_event(event_tx, true);
        self.reset_dynamic_instruction_activation();
        Ok(())
    }

    async fn apply_session_compacting_hook(&mut self, session: &ava_types::Session) {
        let Some(pm) = self.plugin_manager.as_ref() else {
            return;
        };

        let msg_count = self.context.get_messages().len();
        let token_count = self.context.token_count();
        let (extra_context, _custom_prompt) = pm
            .lock()
            .await
            .apply_session_compacting_hook(&session.id.to_string(), msg_count, token_count)
            .await;

        // Plugin-provided custom compaction prompts are not wired yet; only
        // additional context injection is supported in the current runtime path.

        for ctx_str in extra_context {
            self.context
                .add_message(Message::new(Role::System, ctx_str));
        }
    }

    fn emit_context_compacted_event(
        &self,
        event_tx: &Option<mpsc::UnboundedSender<AgentEvent>>,
        auto: bool,
    ) {
        let Some(report) = self.context.last_compaction_report().cloned() else {
            return;
        };
        let active_messages = self
            .context
            .get_agent_visible_messages()
            .into_iter()
            .map(|message| CompactedMessagePreview {
                role: match message.role {
                    Role::System => "system".to_string(),
                    Role::User => "user".to_string(),
                    Role::Assistant => "assistant".to_string(),
                    Role::Tool => "tool".to_string(),
                },
                content: message.content.clone(),
            })
            .collect();

        Self::emit(
            event_tx,
            build_context_compacted_event(&report, self.config.token_limit, auto, active_messages),
        );
    }
}

fn build_context_compacted_event(
    report: &CompactionReport,
    token_limit: usize,
    auto: bool,
    active_messages: Vec<CompactedMessagePreview>,
) -> AgentEvent {
    let usage_before_percent = if token_limit == 0 {
        0.0
    } else {
        (report.tokens_before as f64 / token_limit as f64) * 100.0
    };

    AgentEvent::ContextCompacted {
        auto,
        tokens_before: report.tokens_before,
        tokens_after: report.tokens_after,
        tokens_saved: report.tokens_saved,
        messages_before: report.messages_before,
        messages_after: report.messages_after,
        usage_before_percent,
        summary: format!(
            "Context automatically compacted: {} messages -> summary (saved {} tokens).",
            report.messages_before, report.tokens_saved
        ),
        context_summary: report.summary.clone().unwrap_or_default(),
        active_messages,
    }
}

#[cfg(test)]
mod tests {
    use super::build_context_compacted_event;
    use super::AgentEvent;
    use super::CompactedMessagePreview;
    use ava_context::CompactionReport;

    #[test]
    fn build_context_compacted_event_uses_token_limit() {
        let report = CompactionReport {
            tokens_before: 800,
            tokens_after: 320,
            tokens_saved: 480,
            messages_before: 20,
            messages_after: 8,
            strategy: "summary".to_string(),
            summary: Some("important summary".to_string()),
        };

        let event = build_context_compacted_event(
            &report,
            1000,
            true,
            vec![CompactedMessagePreview {
                role: "assistant".to_string(),
                content: "active message".to_string(),
            }],
        );

        match event {
            AgentEvent::ContextCompacted {
                auto,
                usage_before_percent,
                context_summary,
                active_messages,
                ..
            } => {
                assert!(auto);
                assert_eq!(usage_before_percent, 80.0);
                assert_eq!(context_summary, "important summary");
                assert_eq!(active_messages.len(), 1);
            }
            other => panic!("expected ContextCompacted, got {other:?}"),
        }
    }
}
