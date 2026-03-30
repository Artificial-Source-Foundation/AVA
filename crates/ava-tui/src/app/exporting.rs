use super::*;
use chrono::Local;

/// Format a number with comma separators (e.g. 128000 -> "128,000").
fn format_with_commas(n: usize) -> String {
    let s = n.to_string();
    let mut result = String::with_capacity(s.len() + s.len() / 3);
    for (i, ch) in s.chars().enumerate() {
        if i > 0 && (s.len() - i).is_multiple_of(3) {
            result.push(',');
        }
        result.push(ch);
    }
    result
}

impl App {
    pub(super) fn export_conversation(
        &self,
        filename_arg: Option<&str>,
    ) -> Option<(MessageKind, String)> {
        let messages = &self.state.messages.messages;
        if messages.is_empty() {
            return Some((
                MessageKind::Error,
                "No conversation to export yet.".to_string(),
            ));
        }

        let session_name = self
            .state
            .session
            .current_session
            .as_ref()
            .and_then(|session| session.metadata.get("title"))
            .and_then(|v| v.as_str())
            .unwrap_or("session");

        let model = self.state.agent.current_model_display();
        let now = Local::now();
        let safe_session = session_name
            .chars()
            .map(|ch| {
                if ch.is_ascii_alphanumeric() || ch == '-' || ch == '_' {
                    ch
                } else {
                    '-'
                }
            })
            .collect::<String>()
            .trim_matches('-')
            .to_string();
        let fallback_name = if safe_session.is_empty() {
            "session".to_string()
        } else {
            safe_session
        };

        let filename = filename_arg
            .map(|arg| arg.trim())
            .filter(|arg| !arg.is_empty())
            .map(|arg| arg.to_string())
            .unwrap_or_else(|| format!("ava-{fallback_name}-{}.md", now.format("%Y%m%d-%H%M%S")));

        let is_json = filename.ends_with(".json");
        let msg_count = messages.len();

        let content = if is_json {
            self.export_as_json(messages, session_name, &model, &now)
        } else {
            self.export_as_markdown(messages, session_name, &model, &now)
        };

        let path = std::env::current_dir()
            .unwrap_or_else(|_| std::path::PathBuf::from("."))
            .join(&filename);

        match std::fs::write(&path, content) {
            Ok(()) => {
                let display_path = path.display();
                Some((
                    MessageKind::System,
                    format!("Exported conversation to {display_path} ({msg_count} messages)"),
                ))
            }
            Err(err) => Some((MessageKind::Error, format!("Failed to export: {err}"))),
        }
    }

    fn export_as_markdown(
        &self,
        messages: &[UiMessage],
        session_name: &str,
        model: &str,
        now: &chrono::DateTime<Local>,
    ) -> String {
        let mut out = String::new();
        let date_str = now.format("%Y-%m-%d %H:%M:%S").to_string();
        let msg_count = messages.len();

        out.push_str(&format!("# AVA Session -- {session_name}\n"));
        out.push_str(&format!("Model: {model}\n"));
        out.push_str(&format!("Date: {date_str}\n"));
        out.push_str(&format!("Messages: {msg_count}\n"));
        out.push_str("\n---\n\n");

        for msg in messages.iter().filter(|m| !m.transient) {
            match msg.kind {
                MessageKind::User => {
                    out.push_str("## User\n");
                    out.push_str(&msg.content);
                    out.push_str("\n\n---\n\n");
                }
                MessageKind::Assistant => {
                    out.push_str("## Assistant\n");
                    out.push_str(&msg.content);
                    out.push_str("\n\n---\n\n");
                }
                MessageKind::ToolCall => {
                    let tool_name = msg.content.split_whitespace().next().unwrap_or("unknown");
                    let rest = msg.content[tool_name.len()..].trim_start();
                    out.push_str(&format!("## Tool Call: {tool_name}\n"));
                    if !rest.is_empty() {
                        out.push_str("```yaml\n");
                        out.push_str(rest);
                        out.push_str("\n```\n");
                    }
                    out.push('\n');
                }
                MessageKind::ToolResult => {
                    out.push_str("## Tool Result\n");
                    let content_truncated = crate::text_utils::truncate_display(&msg.content, 500);
                    out.push_str(&content_truncated);
                    if content_truncated.len() != msg.content.len() {
                        out.push_str("\n... (truncated)\n");
                    } else {
                        out.push('\n');
                    }
                    out.push_str("\n---\n\n");
                }
                MessageKind::Thinking => {
                    out.push_str("## Thinking\n");
                    out.push_str(&msg.content);
                    out.push_str("\n\n");
                }
                MessageKind::Error => {
                    out.push_str(&format!("**Error:** {}\n\n", msg.content));
                }
                MessageKind::System => {
                    out.push_str(&format!("*{system}*\n\n", system = msg.content));
                }
                MessageKind::SubAgent => {
                    out.push_str("## Sub-Agent\n");
                    if let Some(sub) = &msg.sub_agent {
                        let mut meta = Vec::new();
                        if let Some(provider) = &sub.provider {
                            meta.push(format!("provider: {provider}"));
                        }
                        if sub.resumed {
                            meta.push("resumed".to_string());
                        }
                        if let Some(cost) = sub.cost_usd {
                            meta.push(format!("cost: ${cost:.4}"));
                        }
                        if let (Some(input), Some(output)) = (sub.input_tokens, sub.output_tokens) {
                            meta.push(format!("tokens: {input}/{output}"));
                        }
                        if !meta.is_empty() {
                            out.push_str(&format!("*{}*\n\n", meta.join(" | ")));
                        }
                    }
                    out.push_str(&msg.content);
                    out.push_str("\n\n---\n\n");
                }
            }
        }

        out
    }

    fn export_as_json(
        &self,
        messages: &[UiMessage],
        session_name: &str,
        model: &str,
        now: &chrono::DateTime<Local>,
    ) -> String {
        let date_str = now.to_rfc3339();

        let json_messages: Vec<serde_json::Value> = messages
            .iter()
            .filter(|m| !m.transient)
            .map(|msg| {
                let role = match msg.kind {
                    MessageKind::User => "user",
                    MessageKind::Assistant => "assistant",
                    MessageKind::ToolCall => "tool_call",
                    MessageKind::ToolResult => "tool_result",
                    MessageKind::Thinking => "thinking",
                    MessageKind::Error => "error",
                    MessageKind::System => "system",
                    MessageKind::SubAgent => "sub_agent",
                };

                let mut obj = serde_json::json!({
                    "role": role,
                    "content": msg.content,
                });

                if msg.kind == MessageKind::ToolCall {
                    let tool_name = msg.content.split_whitespace().next().unwrap_or("unknown");
                    let rest = msg.content[tool_name.len()..].trim_start();
                    obj["name"] = serde_json::json!(tool_name);
                    obj["input"] = serde_json::json!(rest);
                }

                if let Some(ref model_name) = msg.model_name {
                    obj["model"] = serde_json::json!(model_name);
                }

                if let Some(sub) = &msg.sub_agent {
                    obj["sub_agent"] = serde_json::json!({
                        "description": sub.description,
                        "provider": sub.provider,
                        "resumed": sub.resumed,
                        "cost_usd": sub.cost_usd,
                        "input_tokens": sub.input_tokens,
                        "output_tokens": sub.output_tokens,
                        "session_id": sub.session_id,
                    });
                }

                obj
            })
            .collect();

        let export = serde_json::json!({
            "session": session_name,
            "model": model,
            "date": date_str,
            "messages": json_messages,
        });

        serde_json::to_string_pretty(&export).unwrap_or_else(|e| format!("{{\"error\": \"{e}\"}}"))
    }

    pub(super) fn run_compact(&mut self, focus: Option<&str>) -> Option<(MessageKind, String)> {
        use ava_context::strategies::CondensationStrategy;
        use ava_context::{SlidingWindowStrategy, ToolTruncationStrategy};

        let ui_messages = &self.state.messages.messages;
        if ui_messages.is_empty() {
            return Some((
                MessageKind::System,
                "Nothing to compact -- conversation is empty.".to_string(),
            ));
        }

        let typed_messages: Vec<ava_types::Message> = ui_messages
            .iter()
            .map(|ui| {
                let role = match ui.kind {
                    MessageKind::User => ava_types::Role::User,
                    MessageKind::Assistant => ava_types::Role::Assistant,
                    MessageKind::ToolCall => ava_types::Role::Assistant,
                    MessageKind::ToolResult => ava_types::Role::Tool,
                    MessageKind::Thinking => ava_types::Role::Assistant,
                    MessageKind::Error => ava_types::Role::System,
                    MessageKind::System => ava_types::Role::System,
                    MessageKind::SubAgent => ava_types::Role::Assistant,
                };
                ava_types::Message::new(role, &ui.content)
            })
            .collect();

        let before_tokens: usize = typed_messages
            .iter()
            .map(ava_context::estimate_tokens_for_message)
            .sum();
        let before_count = ui_messages.len();

        let context_window = self.state.agent.context_window.unwrap_or(128_000);
        let usage_pct = before_tokens as f64 / context_window as f64 * 100.0;

        if usage_pct < 50.0 && focus.is_none() {
            self.set_status(
                format!("Context: {}% -- no compaction needed", usage_pct as u64),
                StatusLevel::Info,
            );
            return Some((
                MessageKind::System,
                format!(
                    "Context usage is low ({:.0}%), no compaction needed. \n{} tokens across {before_count} messages.",
                    usage_pct,
                    format_with_commas(before_tokens),
                ),
            ));
        }

        let target_tokens = (context_window / 2).min(before_tokens * 3 / 4);

        let truncated = ToolTruncationStrategy::default()
            .condense(&typed_messages, target_tokens)
            .unwrap_or_else(|_| typed_messages.clone());

        let condensed = SlidingWindowStrategy
            .condense(&truncated, target_tokens)
            .unwrap_or(truncated);

        let final_messages = if let Some(focus_text) = focus {
            let keywords: Vec<&str> = focus_text.split_whitespace().collect();
            let mut kept_indices: Vec<bool> = vec![false; typed_messages.len()];

            let condensed_set: std::collections::HashSet<String> =
                condensed.iter().map(|m| m.content.clone()).collect();
            for (i, msg) in typed_messages.iter().enumerate() {
                if condensed_set.contains(&msg.content) {
                    kept_indices[i] = true;
                }
            }

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

            let mut focused: Vec<ava_types::Message> = typed_messages
                .iter()
                .zip(kept_indices.iter())
                .filter(|(_, kept)| **kept)
                .map(|(m, _)| m.clone())
                .collect();

            let mut total: usize = focused
                .iter()
                .map(ava_context::estimate_tokens_for_message)
                .sum();
            while total > target_tokens && focused.len() > 1 {
                let removed = focused.remove(0);
                total -= ava_context::estimate_tokens_for_message(&removed);
            }
            focused
        } else {
            condensed
        };

        let after_tokens: usize = final_messages
            .iter()
            .map(ava_context::estimate_tokens_for_message)
            .sum();
        let after_count = final_messages.len();
        let saved_tokens = before_tokens.saturating_sub(after_tokens);
        let dropped_count = before_count.saturating_sub(after_count);

        if dropped_count == 0 {
            self.set_status("Already compact".to_string(), StatusLevel::Info);
            return Some((
                MessageKind::System,
                format!(
                    "Conversation is already compact.\n{} tokens across {before_count} messages.",
                    format_with_commas(before_tokens),
                ),
            ));
        }

        let summary = if let Some(focus_text) = focus {
            format!(
                "Compacted conversation (focus: \"{focus_text}\"). Saved ~{} tokens (was {}, now {}). Dropped {dropped_count} messages, kept {after_count}.",
                    format_with_commas(saved_tokens),
                    format_with_commas(before_tokens),
                    format_with_commas(after_tokens)
            )
        } else {
            format!(
                "Compacted conversation. Saved ~{} tokens (was {}, now {}). Dropped {dropped_count} messages, kept {after_count}.",
                    format_with_commas(saved_tokens),
                    format_with_commas(before_tokens),
                    format_with_commas(after_tokens)
            )
        };

        let mut new_ui_messages: Vec<UiMessage> = Vec::with_capacity(after_count + 1);
        new_ui_messages.push(UiMessage::new(MessageKind::System, &summary));

        for condensed_msg in &final_messages {
            let matching_ui = ui_messages
                .iter()
                .find(|ui| ui.content == condensed_msg.content)
                .or_else(|| {
                    ui_messages.iter().find(|ui| {
                        condensed_msg.content.len() > 10
                            && ui.content.starts_with(
                                &condensed_msg.content[..condensed_msg.content.len().min(50)],
                            )
                    })
                });

            if let Some(original) = matching_ui {
                let mut rebuilt = original.clone();
                if rebuilt.content != condensed_msg.content {
                    rebuilt.content = condensed_msg.content.clone();
                }
                rebuilt.is_streaming = false;
                new_ui_messages.push(rebuilt);
            } else {
                let kind = match condensed_msg.role {
                    ava_types::Role::User => MessageKind::User,
                    ava_types::Role::Assistant => MessageKind::Assistant,
                    ava_types::Role::Tool => MessageKind::ToolResult,
                    ava_types::Role::System => MessageKind::System,
                };
                new_ui_messages.push(UiMessage::new(kind, &condensed_msg.content));
            }
        }

        self.state.messages.messages = new_ui_messages;
        self.state.messages.reset_scroll();

        self.set_status(
            format!("Compacted: saved ~{saved_tokens} tokens"),
            StatusLevel::Info,
        );
        None
    }
}
