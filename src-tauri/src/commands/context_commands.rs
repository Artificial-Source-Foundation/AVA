//! Tauri commands for context compaction.

use std::sync::Arc;

use async_trait::async_trait;
use ava_agent::control_plane::sessions::resolve_existing_session;
use ava_context::{estimate_tokens_for_message, CondenserConfig, HybridCondenser};
use ava_llm::provider::LLMProvider;
use ava_types::{Message, Role, Session};
use chrono::Utc;
use serde::{Deserialize, Serialize};
use tauri::State;
use uuid::Uuid;

use crate::bridge::DesktopBridge;

const DEFAULT_CONTEXT_WINDOW: usize = 128_000;

#[derive(Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct CompactMessage {
    pub role: String,
    pub content: String,
}

#[derive(Serialize, Clone)]
#[serde(rename_all = "camelCase")]
pub struct CompactMessageOut {
    pub role: String,
    pub content: String,
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub struct CompactContextResult {
    pub messages: Vec<CompactMessageOut>,
    pub tokens_before: usize,
    pub tokens_after: usize,
    pub tokens_saved: usize,
    pub messages_before: usize,
    pub messages_after: usize,
    pub summary: String,
    pub context_summary: String,
    pub usage_before_percent: f64,
}

struct DesktopSummarizer(Arc<dyn LLMProvider>);

#[async_trait]
impl ava_context::Summarizer for DesktopSummarizer {
    async fn summarize(&self, text: &str) -> std::result::Result<String, String> {
        let messages = vec![Message::new(Role::User, text.to_string())];
        self.0.generate(&messages).await.map_err(|e| e.to_string())
    }
}

fn parse_role(role: &str) -> Role {
    match role {
        "user" => Role::User,
        "assistant" => Role::Assistant,
        "tool" => Role::Tool,
        _ => Role::System,
    }
}

fn role_to_string(role: &Role) -> &'static str {
    match role {
        Role::User => "user",
        Role::Assistant => "assistant",
        Role::Tool => "tool",
        Role::System => "system",
    }
}

fn extract_context_summary(messages: &[Message]) -> Option<String> {
    messages
        .iter()
        .rev()
        .find(|message| {
            message.role == Role::System && message.content.starts_with("## Conversation Summary")
        })
        .map(|message| message.content.clone())
}

fn is_compaction_summary(message: &Message) -> bool {
    message.role == Role::System && message.content.starts_with("## Conversation Summary")
}

fn build_summary_line(
    focus: Option<&str>,
    tokens_before: usize,
    tokens_after: usize,
    messages_before: usize,
    messages_after: usize,
) -> String {
    let saved = tokens_before.saturating_sub(tokens_after);
    let condensed = messages_before.saturating_sub(messages_after);
    match focus.filter(|value| !value.trim().is_empty()) {
        Some(focus) => format!(
            "Conversation compacted (focus: {focus}): {messages_before} messages -> summary (saved {saved} tokens, condensed {condensed} messages)."
        ),
        None => format!(
            "Conversation compacted: {messages_before} messages -> summary (saved {saved} tokens, condensed {condensed} messages)."
        ),
    }
}

fn already_compact_result(
    messages: &[Message],
    tokens: usize,
    usage_before_percent: f64,
) -> CompactContextResult {
    CompactContextResult {
        messages: messages
            .iter()
            .map(|message| CompactMessageOut {
                role: role_to_string(&message.role).to_string(),
                content: message.content.clone(),
            })
            .collect(),
        tokens_before: tokens,
        tokens_after: tokens,
        tokens_saved: 0,
        messages_before: messages.len(),
        messages_after: messages.len(),
        summary: format!(
            "Conversation is already compact. {tokens} tokens across {} messages.",
            messages.len()
        ),
        context_summary: String::new(),
        usage_before_percent,
    }
}

fn to_frontend_messages(messages: &[Message]) -> Vec<CompactMessageOut> {
    messages
        .iter()
        .map(|message| CompactMessageOut {
            role: role_to_string(&message.role).to_string(),
            content: message.content.clone(),
        })
        .collect()
}

fn hydrate_session(session_id: Uuid, existing: Option<Session>, messages: Vec<Message>) -> Session {
    let mut session = existing.unwrap_or_else(|| Session::new().with_id(session_id));
    session.messages = messages;
    session.updated_at = Utc::now();
    session
}

async fn save_session(
    bridge: &DesktopBridge,
    session_id: Uuid,
    existing: Option<Session>,
    messages: Vec<Message>,
) -> Result<(), String> {
    let session_manager = bridge.stack.session_manager.clone();
    let session = hydrate_session(session_id, existing, messages);
    tokio::task::spawn_blocking(move || session_manager.save(&session))
        .await
        .map_err(|e| format!("session save join error: {e}"))?
        .map_err(|e| e.to_string())?;
    *bridge.last_session_id.write().await = Some(session_id);
    Ok(())
}

async fn resolve_compaction_provider(
    bridge: &DesktopBridge,
    compaction_provider: Option<&str>,
    compaction_model: Option<&str>,
) -> Result<Arc<dyn LLMProvider>, String> {
    if let (Some(provider), Some(model)) = (compaction_provider, compaction_model) {
        return bridge
            .stack
            .router
            .route_required(provider, model)
            .await
            .map_err(|e| e.to_string());
    }

    let (provider, model) = bridge.stack.current_model().await;
    bridge
        .stack
        .router
        .route_required(&provider, &model)
        .await
        .map_err(|e| e.to_string())
}

async fn build_condenser(
    bridge: &DesktopBridge,
    context_window: usize,
    focus: Option<String>,
    compaction_provider: Option<&str>,
    compaction_model: Option<&str>,
) -> Result<HybridCondenser, String> {
    let provider =
        resolve_compaction_provider(bridge, compaction_provider, compaction_model).await?;
    let summarizer: Arc<dyn ava_context::Summarizer> = Arc::new(DesktopSummarizer(provider));
    let config = CondenserConfig {
        max_tokens: context_window,
        target_tokens: context_window * 3 / 4,
        preserve_recent_messages: 4,
        preserve_recent_turns: 2,
        focus,
        ..Default::default()
    };

    Ok(ava_context::create_hybrid_condenser(
        config,
        Some(summarizer),
    ))
}

fn simple_messages(messages: Vec<CompactMessage>) -> Vec<Message> {
    messages
        .into_iter()
        .map(|message| Message::new(parse_role(&message.role), message.content))
        .collect()
}

#[tauri::command]
pub async fn compact_context(
    messages: Vec<CompactMessage>,
    focus: Option<String>,
    context_window: Option<usize>,
    session_id: Option<String>,
    compaction_provider: Option<String>,
    compaction_model: Option<String>,
    bridge: State<'_, DesktopBridge>,
) -> Result<CompactContextResult, String> {
    let requested_session_id = session_id
        .as_deref()
        .map(Uuid::parse_str)
        .transpose()
        .map_err(|e| format!("invalid session id: {e}"))?;
    let session_uuid =
        resolve_existing_session(requested_session_id, *bridge.last_session_id.read().await)
            .map(|selection| selection.session_id);

    let existing_session =
        session_uuid.and_then(|id| bridge.stack.session_manager.get(id).ok().flatten());

    let source_messages = existing_session
        .as_ref()
        .map(|session| session.messages.clone())
        .filter(|session_messages| !session_messages.is_empty())
        .unwrap_or_else(|| simple_messages(messages));

    if source_messages.is_empty() {
        return Ok(CompactContextResult {
            messages: Vec::new(),
            tokens_before: 0,
            tokens_after: 0,
            tokens_saved: 0,
            messages_before: 0,
            messages_after: 0,
            summary: "Nothing to compact -- conversation is empty.".to_string(),
            context_summary: String::new(),
            usage_before_percent: 0.0,
        });
    }

    let window = context_window.unwrap_or(DEFAULT_CONTEXT_WINDOW);
    let tokens_before: usize = source_messages
        .iter()
        .map(estimate_tokens_for_message)
        .sum();
    let usage_before_percent = if window == 0 {
        0.0
    } else {
        (tokens_before as f64 / window as f64) * 100.0
    };

    let mut condenser = build_condenser(
        &bridge,
        window,
        focus.clone(),
        compaction_provider.as_deref(),
        compaction_model.as_deref(),
    )
    .await?;

    let condensed = condenser
        .force_condense(&source_messages)
        .await
        .map_err(|e| e.to_string())?;

    let tokens_after = condensed.estimated_tokens;
    let messages_after = condensed.messages.len();
    let context_summary = extract_context_summary(&condensed.messages).unwrap_or_default();

    if messages_after >= source_messages.len() && tokens_after >= tokens_before {
        return Ok(already_compact_result(
            &source_messages,
            tokens_before,
            usage_before_percent,
        ));
    }

    if let Some(session_uuid) = session_uuid {
        let mut active_messages = condensed.messages.clone();
        if let Some(summary_index) = active_messages.iter().position(is_compaction_summary) {
            let next_timestamp = active_messages
                .iter()
                .skip(summary_index + 1)
                .find(|message| !is_compaction_summary(message))
                .map(|message| message.timestamp);
            let previous_timestamp = condensed
                .compacted_messages
                .last()
                .map(|message| message.timestamp)
                .or_else(|| {
                    active_messages[..summary_index]
                        .iter()
                        .rev()
                        .find(|message| !is_compaction_summary(message))
                        .map(|message| message.timestamp)
                });

            if let Some(summary) = active_messages.get_mut(summary_index) {
                if let Some(next_timestamp) = next_timestamp {
                    summary.timestamp = next_timestamp - chrono::Duration::milliseconds(1);
                } else if let Some(previous_timestamp) = previous_timestamp {
                    summary.timestamp = previous_timestamp + chrono::Duration::milliseconds(1);
                }
            }
        }

        let mut persisted_messages = condensed.compacted_messages.clone();
        persisted_messages.extend(active_messages);
        persisted_messages.sort_by_key(|message| message.timestamp);
        save_session(&bridge, session_uuid, existing_session, persisted_messages).await?;
    }

    let summary = build_summary_line(
        focus.as_deref(),
        tokens_before,
        tokens_after,
        source_messages.len(),
        messages_after,
    );

    Ok(CompactContextResult {
        messages: to_frontend_messages(&condensed.messages),
        tokens_before,
        tokens_after,
        tokens_saved: tokens_before.saturating_sub(tokens_after),
        messages_before: source_messages.len(),
        messages_after,
        summary,
        context_summary,
        usage_before_percent,
    })
}
