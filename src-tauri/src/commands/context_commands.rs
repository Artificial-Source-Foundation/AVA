//! Tauri commands for context compaction.
//!
//! Mirrors the TUI `/compact` command: takes conversation messages, applies
//! tool-truncation + sliding-window strategies (with optional focus filtering),
//! and returns the compacted result with token stats.

use ava_context::strategies::CondensationStrategy;
use ava_context::{estimate_tokens_for_message, SlidingWindowStrategy, ToolTruncationStrategy};
use ava_types::{Message, Role};
use serde::{Deserialize, Serialize};

/// A single message as sent from the SolidJS frontend.
#[derive(Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct CompactMessage {
    pub role: String,
    pub content: String,
}

/// Result returned to the frontend after compaction.
#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub struct CompactContextResult {
    /// Compacted messages (role + content).
    pub messages: Vec<CompactMessageOut>,
    /// Token count before compaction.
    pub tokens_before: usize,
    /// Token count after compaction.
    pub tokens_after: usize,
    /// Number of tokens saved.
    pub tokens_saved: usize,
    /// Number of messages before compaction.
    pub messages_before: usize,
    /// Number of messages after compaction.
    pub messages_after: usize,
    /// Human-readable summary of the compaction.
    pub summary: String,
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
pub struct CompactMessageOut {
    pub role: String,
    pub content: String,
}

fn parse_role(s: &str) -> Role {
    match s {
        "user" => Role::User,
        "assistant" => Role::Assistant,
        "tool" => Role::Tool,
        _ => Role::System,
    }
}

fn role_to_string(role: Role) -> &'static str {
    match role {
        Role::User => "user",
        Role::Assistant => "assistant",
        Role::Tool => "tool",
        Role::System => "system",
    }
}

/// Compact conversation context to save tokens.
///
/// Takes the current conversation messages and an optional focus keyword.
/// Returns the compacted messages along with before/after token stats.
#[tauri::command]
pub async fn compact_context(
    messages: Vec<CompactMessage>,
    focus: Option<String>,
    context_window: Option<usize>,
) -> Result<CompactContextResult, String> {
    if messages.is_empty() {
        return Ok(CompactContextResult {
            messages: vec![],
            tokens_before: 0,
            tokens_after: 0,
            tokens_saved: 0,
            messages_before: 0,
            messages_after: 0,
            summary: "Nothing to compact -- conversation is empty.".to_string(),
        });
    }

    // Convert frontend messages to ava_types::Message
    let typed_messages: Vec<Message> = messages
        .iter()
        .map(|m| Message::new(parse_role(&m.role), &m.content))
        .collect();

    let before_tokens: usize = typed_messages.iter().map(estimate_tokens_for_message).sum();
    let before_count = messages.len();

    let ctx_window = context_window.unwrap_or(128_000);
    let usage_pct = before_tokens as f64 / ctx_window as f64 * 100.0;

    // If usage is low and no focus keyword, skip compaction
    if usage_pct < 50.0 && focus.is_none() {
        let out_messages: Vec<CompactMessageOut> = messages
            .iter()
            .map(|m| CompactMessageOut {
                role: m.role.clone(),
                content: m.content.clone(),
            })
            .collect();
        return Ok(CompactContextResult {
            messages: out_messages,
            tokens_before: before_tokens,
            tokens_after: before_tokens,
            tokens_saved: 0,
            messages_before: before_count,
            messages_after: before_count,
            summary: format!(
                "Context usage is low ({:.0}%), no compaction needed. {} tokens across {} messages.",
                usage_pct, before_tokens, before_count,
            ),
        });
    }

    let target_tokens = (ctx_window / 2).min(before_tokens * 3 / 4);

    // Stage 1: Truncate long tool outputs
    let truncated = ToolTruncationStrategy::default()
        .condense(&typed_messages, target_tokens)
        .unwrap_or_else(|_| typed_messages.clone());

    // Stage 2: Sliding window to drop oldest messages
    let condensed = SlidingWindowStrategy
        .condense(&truncated, target_tokens)
        .unwrap_or(truncated);

    // Stage 3 (optional): Focus filtering
    let final_messages = if let Some(ref focus_text) = focus {
        let keywords: Vec<&str> = focus_text.split_whitespace().collect();
        let mut kept_indices: Vec<bool> = vec![false; typed_messages.len()];

        let condensed_set: std::collections::HashSet<String> =
            condensed.iter().map(|m| m.content.clone()).collect();
        for (i, msg) in typed_messages.iter().enumerate() {
            if condensed_set.contains(&msg.content) {
                kept_indices[i] = true;
            }
        }

        // Also keep messages matching any focus keyword
        for (i, msg) in typed_messages.iter().enumerate() {
            if !kept_indices[i] {
                let content_lower = msg.content.to_lowercase();
                if keywords
                    .iter()
                    .any(|kw| content_lower.contains(&kw.to_lowercase()))
                {
                    kept_indices[i] = true;
                }
            }
        }

        let mut focused: Vec<Message> = typed_messages
            .iter()
            .zip(kept_indices.iter())
            .filter(|(_, kept)| **kept)
            .map(|(m, _)| m.clone())
            .collect();

        // Trim from the front if still over target
        let mut total: usize = focused.iter().map(estimate_tokens_for_message).sum();
        while total > target_tokens && focused.len() > 1 {
            let removed = focused.remove(0);
            total -= estimate_tokens_for_message(&removed);
        }
        focused
    } else {
        condensed
    };

    let after_tokens: usize = final_messages.iter().map(estimate_tokens_for_message).sum();
    let after_count = final_messages.len();
    let saved_tokens = before_tokens.saturating_sub(after_tokens);
    let dropped_count = before_count.saturating_sub(after_count);

    if dropped_count == 0 {
        let out_messages: Vec<CompactMessageOut> = final_messages
            .iter()
            .map(|m| CompactMessageOut {
                role: role_to_string(m.role.clone()).to_string(),
                content: m.content.clone(),
            })
            .collect();
        return Ok(CompactContextResult {
            messages: out_messages,
            tokens_before: before_tokens,
            tokens_after: after_tokens,
            tokens_saved: saved_tokens,
            messages_before: before_count,
            messages_after: after_count,
            summary: format!(
                "Conversation is already compact. {} tokens across {} messages.",
                before_tokens, before_count,
            ),
        });
    }

    let summary = if let Some(ref focus_text) = focus {
        format!(
            "Compacted conversation (focus: \"{focus_text}\"). Saved ~{saved_tokens} tokens (was {before_tokens}, now {after_tokens}). Dropped {dropped_count} messages, kept {after_count}.",
        )
    } else {
        format!(
            "Compacted conversation. Saved ~{saved_tokens} tokens (was {before_tokens}, now {after_tokens}). Dropped {dropped_count} messages, kept {after_count}.",
        )
    };

    let out_messages: Vec<CompactMessageOut> = final_messages
        .iter()
        .map(|m| CompactMessageOut {
            role: role_to_string(m.role.clone()).to_string(),
            content: m.content.clone(),
        })
        .collect();

    Ok(CompactContextResult {
        messages: out_messages,
        tokens_before: before_tokens,
        tokens_after: after_tokens,
        tokens_saved: saved_tokens,
        messages_before: before_count,
        messages_after: after_count,
        summary,
    })
}
