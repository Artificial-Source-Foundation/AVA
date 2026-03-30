use std::collections::BTreeSet;
use std::sync::Arc;

use async_trait::async_trait;
use ava_types::{Message, Role};

use super::{AsyncCondensationStrategy, Summarizer};
use crate::error::Result;
use crate::token_tracker::estimate_tokens_for_message;

pub struct SummarizationStrategy {
    summarizer: Option<Arc<dyn Summarizer>>,
    batch_size: usize,
    preserve_recent: usize,
    preserve_recent_turns: usize,
    focus: Option<String>,
    previous_summary: Option<String>,
}

struct SummaryData {
    task: String,
    progress: Vec<String>,
    decisions: Vec<String>,
    files_touched: Vec<String>,
    current_state: String,
    open_issues: Vec<String>,
}

impl SummaryData {
    fn to_markdown(&self) -> String {
        let progress = if self.progress.is_empty() {
            "No substantial progress recorded.".to_string()
        } else {
            self.progress.join(" ")
        };
        let decisions = if self.decisions.is_empty() {
            "- None recorded".to_string()
        } else {
            self.decisions
                .iter()
                .map(|item| format!("- {item}"))
                .collect::<Vec<_>>()
                .join("\n")
        };
        let files = if self.files_touched.is_empty() {
            "- None recorded".to_string()
        } else {
            self.files_touched
                .iter()
                .map(|item| format!("- {item}"))
                .collect::<Vec<_>>()
                .join("\n")
        };
        let issues = if self.open_issues.is_empty() {
            "- None".to_string()
        } else {
            self.open_issues
                .iter()
                .map(|item| format!("- {item}"))
                .collect::<Vec<_>>()
                .join("\n")
        };

        format!(
            "## Conversation Summary\n\
             **Task**: {}\n\
             **Progress**: {}\n\
             **Key Decisions**:\n{}\n\
             **Files Touched**:\n{}\n\
             **Current State**: {}\n\
             **Open Issues**:\n{}",
            self.task, progress, decisions, files, self.current_state, issues
        )
    }
}

impl SummarizationStrategy {
    pub fn new(
        summarizer: Option<Arc<dyn Summarizer>>,
        batch_size: usize,
        preserve_recent: usize,
        preserve_recent_turns: usize,
        focus: Option<String>,
    ) -> Self {
        Self {
            summarizer,
            batch_size,
            preserve_recent,
            preserve_recent_turns,
            focus,
            previous_summary: None,
        }
    }

    pub fn set_previous_summary(&mut self, summary: Option<String>) {
        self.previous_summary = summary;
    }

    pub fn previous_summary(&self) -> Option<&str> {
        self.previous_summary.as_deref()
    }

    fn is_pinned(message: &Message) -> bool {
        message
            .metadata
            .get("pinned")
            .and_then(|value| value.as_bool())
            .unwrap_or(false)
    }

    fn focus_keywords(focus: Option<&str>) -> Vec<String> {
        focus
            .unwrap_or_default()
            .split(|ch: char| ch.is_whitespace() || ch == ',' || ch == ':')
            .map(str::trim)
            .filter(|item| !item.is_empty())
            .map(str::to_lowercase)
            .collect()
    }

    fn message_matches_focus(message: &Message, keywords: &[String]) -> bool {
        if keywords.is_empty() {
            return false;
        }

        let mut haystack = message.content.to_lowercase();

        for call in &message.tool_calls {
            haystack.push('\n');
            haystack.push_str(&call.name.to_lowercase());
            haystack.push('\n');
            haystack.push_str(&call.arguments.to_string().to_lowercase());
        }

        for result in &message.tool_results {
            haystack.push('\n');
            haystack.push_str(&result.content.to_lowercase());
        }

        keywords.iter().any(|keyword| haystack.contains(keyword))
    }

    fn extract_paths(text: &str, files: &mut Vec<String>) {
        for word in text.split_whitespace() {
            let candidate = word.trim_matches(|ch: char| {
                !ch.is_alphanumeric() && ch != '/' && ch != '.' && ch != '_' && ch != '-'
            });
            let looks_like_path = candidate.starts_with('/')
                || candidate.starts_with("./")
                || candidate.starts_with("src/")
                || candidate.starts_with("crates/")
                || candidate.ends_with(".rs")
                || candidate.ends_with(".ts")
                || candidate.ends_with(".tsx")
                || candidate.ends_with(".js")
                || candidate.ends_with(".json")
                || candidate.ends_with(".md");
            if looks_like_path
                && !candidate.is_empty()
                && !files.iter().any(|item| item == candidate)
            {
                files.push(candidate.to_string());
            }
        }
    }

    fn push_unique(items: &mut Vec<String>, value: String, limit: usize) {
        if value.is_empty() || items.iter().any(|item| item == &value) {
            return;
        }
        if items.len() < limit {
            items.push(value);
        }
    }

    fn collect_summary_data(
        messages: &[Message],
        previous_summary: Option<&str>,
        focus: Option<&str>,
    ) -> SummaryData {
        let mut task = previous_summary
            .and_then(|summary| {
                summary
                    .lines()
                    .find_map(|line| line.strip_prefix("**Task**: ").map(str::trim))
            })
            .unwrap_or("Continue the active coding task.")
            .to_string();
        let mut progress = Vec::new();
        let mut decisions = Vec::new();
        let mut files_touched = Vec::new();
        let mut current_state = previous_summary
            .and_then(|summary| {
                summary
                    .lines()
                    .find_map(|line| line.strip_prefix("**Current State**: ").map(str::trim))
            })
            .unwrap_or("Conversation is mid-task.")
            .to_string();
        let mut open_issues = Vec::new();

        if let Some(summary) = previous_summary {
            for line in summary.lines() {
                if let Some(path) = line.strip_prefix("- ") {
                    let path = path.trim();
                    let looks_like_path = path.contains('/') || path.contains('.');
                    if looks_like_path {
                        Self::push_unique(&mut files_touched, path.to_string(), 16);
                    }
                }
            }
        }

        let focus_label = focus.filter(|item| !item.trim().is_empty()).map(str::trim);
        let mut last_user = None;
        let mut last_non_empty = None;

        for message in messages {
            if !message.content.trim().is_empty() {
                last_non_empty = Some(message.content.trim().to_string());
            }

            if message.role == Role::User && !message.content.trim().is_empty() {
                last_user = Some(message.content.trim().to_string());
            }

            Self::extract_paths(&message.content, &mut files_touched);

            for call in &message.tool_calls {
                if let Some(path) = call
                    .arguments
                    .get("file_path")
                    .or_else(|| call.arguments.get("path"))
                    .and_then(|value| value.as_str())
                {
                    Self::push_unique(&mut files_touched, path.to_string(), 16);
                }
                Self::push_unique(
                    &mut progress,
                    format!("Used `{}` during the preserved work.", call.name),
                    8,
                );
            }

            for result in &message.tool_results {
                if result.is_error {
                    let snippet = result
                        .content
                        .lines()
                        .next()
                        .unwrap_or(result.content.as_str());
                    Self::push_unique(&mut open_issues, snippet.trim().to_string(), 6);
                }
            }

            if message.role == Role::Assistant {
                let trimmed = message.content.trim();
                if !trimmed.is_empty() {
                    if trimmed.len() <= 280 {
                        Self::push_unique(&mut decisions, trimmed.to_string(), 6);
                    }
                    if progress.len() < 8 {
                        Self::push_unique(&mut progress, trimmed.to_string(), 8);
                    }
                }
            }
        }

        if let Some(user) = last_user {
            task = user;
        }
        if let Some(state) = last_non_empty {
            current_state = state;
        }

        if let Some(label) = focus_label {
            Self::push_unique(
                &mut decisions,
                format!("Preserved extra detail for focus `{label}`."),
                6,
            );
        }

        SummaryData {
            task,
            progress,
            decisions,
            files_touched,
            current_state,
            open_issues,
        }
    }

    fn heuristic_group_summary(messages: &[Message], focus: Option<&str>) -> String {
        let data = Self::collect_summary_data(messages, None, focus);
        let mut parts = Vec::new();
        parts.push(format!("Task: {}.", data.task));
        if !data.progress.is_empty() {
            parts.push(format!("Progress: {}.", data.progress.join(" ")));
        }
        if !data.files_touched.is_empty() {
            parts.push(format!("Files: {}.", data.files_touched.join(", ")));
        }
        if !data.decisions.is_empty() {
            parts.push(format!("Key decisions: {}.", data.decisions.join(" ")));
        }
        parts.push(format!("Current state: {}.", data.current_state));
        if !data.open_issues.is_empty() {
            parts.push(format!("Open issues: {}.", data.open_issues.join("; ")));
        }
        parts.join(" ")
    }

    fn heuristic_summary(
        messages: &[Message],
        previous_summary: Option<&str>,
        focus: Option<&str>,
    ) -> String {
        Self::collect_summary_data(messages, previous_summary, focus).to_markdown()
    }

    fn format_messages_for_prompt(messages: &[Message]) -> String {
        let mut parts = Vec::new();
        for message in messages {
            let role = match message.role {
                Role::System => "system",
                Role::User => "user",
                Role::Assistant => "assistant",
                Role::Tool => "tool",
            };
            if !message.content.trim().is_empty() {
                parts.push(format!("[{role}] {}", message.content.trim()));
            }
            for result in &message.tool_results {
                let status = if result.is_error { "error" } else { "ok" };
                let snippet: String = result.content.chars().take(600).collect();
                parts.push(format!("[tool_result:{status}] {snippet}"));
            }
        }
        parts.join("\n")
    }

    fn recent_turn_boundary(messages: &[Message], preserve_recent_turns: usize) -> usize {
        if preserve_recent_turns == 0 || messages.is_empty() {
            return messages.len();
        }

        let mut seen = 0;
        for idx in (0..messages.len()).rev() {
            if messages[idx].role == Role::User {
                seen += 1;
                if seen >= preserve_recent_turns {
                    return idx;
                }
            }
        }
        0
    }

    fn find_safe_cut_point(messages: &[Message], target_index: usize) -> usize {
        let target = target_index.min(messages.len());
        if target == 0 || target >= messages.len() {
            return target;
        }

        if messages[target].role == Role::Tool {
            let mut idx = target;
            while idx > 0 && messages[idx].role == Role::Tool {
                idx -= 1;
            }
            if messages[idx].role == Role::Assistant && !messages[idx].tool_calls.is_empty() {
                return idx;
            }
        }

        if target > 0
            && messages[target - 1].role == Role::Assistant
            && !messages[target - 1].tool_calls.is_empty()
        {
            let mut end = target;
            while end < messages.len() && messages[end].role == Role::Tool {
                end += 1;
            }
            return end;
        }

        target
    }

    fn expand_protected_indices(
        messages: &[Message],
        indices: &BTreeSet<usize>,
    ) -> BTreeSet<usize> {
        let mut expanded = indices.clone();

        for idx in indices.iter().copied() {
            if idx >= messages.len() {
                continue;
            }

            let mut start = idx;
            let mut end = idx;

            if messages[idx].role == Role::Tool {
                while start > 0 && messages[start - 1].role == Role::Tool {
                    start -= 1;
                }
                if start > 0
                    && messages[start - 1].role == Role::Assistant
                    && !messages[start - 1].tool_calls.is_empty()
                {
                    start -= 1;
                }
            }

            if messages[idx].role == Role::Assistant && !messages[idx].tool_calls.is_empty() {
                while end + 1 < messages.len() && messages[end + 1].role == Role::Tool {
                    end += 1;
                }
            }

            for protect in start..=end {
                expanded.insert(protect);
            }
        }

        expanded
    }

    fn partition_messages(
        messages: &[Message],
        preserve_recent: usize,
        preserve_recent_turns: usize,
        focus: Option<&str>,
    ) -> (Vec<Message>, Vec<Message>, Vec<Message>) {
        let mut system_messages = Vec::new();
        let mut non_system = Vec::new();

        for message in messages {
            if message.role == Role::System {
                system_messages.push(message.clone());
            } else {
                non_system.push(message.clone());
            }
        }

        if non_system.is_empty() {
            return (system_messages, Vec::new(), Vec::new());
        }

        let recent_boundary = non_system.len().saturating_sub(preserve_recent);
        let turn_boundary = Self::recent_turn_boundary(&non_system, preserve_recent_turns);
        let mut protected = BTreeSet::new();
        let safe_boundary =
            Self::find_safe_cut_point(&non_system, recent_boundary.min(turn_boundary));
        for idx in safe_boundary..non_system.len() {
            protected.insert(idx);
        }

        let focus_keywords = Self::focus_keywords(focus);
        for (idx, message) in non_system.iter().enumerate() {
            if Self::is_pinned(message) || Self::message_matches_focus(message, &focus_keywords) {
                protected.insert(idx);
            }
        }

        let protected = Self::expand_protected_indices(&non_system, &protected);
        let mut summarize = Vec::new();
        let mut preserved = Vec::new();

        for (idx, message) in non_system.into_iter().enumerate() {
            if protected.contains(&idx) {
                preserved.push(message);
            } else {
                summarize.push(message);
            }
        }

        (system_messages, summarize, preserved)
    }

    fn group_messages(messages: &[Message], batch_size: usize) -> Vec<Vec<Message>> {
        if messages.is_empty() {
            return Vec::new();
        }

        let mut groups = Vec::new();
        let mut current = Vec::new();
        let mut turns = 0_usize;
        let batch_size = batch_size.max(1);

        for message in messages {
            let starts_new_turn = message.role == Role::User && !current.is_empty();
            let exceeds_turn_budget = starts_new_turn && turns >= 3;
            let exceeds_size_budget = current.len() >= batch_size;

            if exceeds_turn_budget || exceeds_size_budget {
                groups.push(std::mem::take(&mut current));
                turns = 0;
            }

            if message.role == Role::User {
                turns += 1;
            }
            current.push(message.clone());
        }

        if !current.is_empty() {
            groups.push(current);
        }

        groups
    }

    async fn summarize_group(&self, messages: &[Message], ordinal: usize, total: usize) -> String {
        let focus = self.focus.as_deref();
        if let Some(summarizer) = &self.summarizer {
            let formatted = Self::format_messages_for_prompt(messages);
            let focus_hint = focus.unwrap_or("none");
            let prompt = format!(
                "You are summarizing slice {ordinal} of {total} from an AI coding conversation.\n\
                 Focus hint: {focus_hint}.\n\
                 Preserve: key task progress, files touched, important decisions, active errors, and what needs to happen next.\n\
                 Drop: redundant tool output, repeated explanations, and resolved back-and-forth.\n\
                 Return exactly one concise paragraph (4-7 sentences).\n\n{formatted}"
            );
            if let Ok(summary) = summarizer.summarize(&prompt).await {
                if !summary.trim().is_empty() {
                    return summary.trim().to_string();
                }
            }
        }

        Self::heuristic_group_summary(messages, focus)
    }

    async fn merge_summaries(&self, messages: &[Message], group_summaries: &[String]) -> String {
        let previous_summary = self.previous_summary.as_deref();
        let focus = self.focus.as_deref();

        if let Some(summarizer) = &self.summarizer {
            let focus_hint = focus.unwrap_or("none");
            let prior = previous_summary
                .map(|summary| format!("Previous summary:\n{summary}\n\n"))
                .unwrap_or_default();
            let grouped = group_summaries
                .iter()
                .enumerate()
                .map(|(idx, summary)| format!("Group {}:\n{}", idx + 1, summary.trim()))
                .collect::<Vec<_>>()
                .join("\n\n");
            let prompt = format!(
                "You are summarizing an AI coding conversation for context continuity.\n\
                 Preserve: key decisions made, files modified, errors encountered, current task state.\n\
                 Drop: redundant tool outputs, repeated explanations, resolved back-and-forth.\n\
                 Focus hint: {focus_hint}.\n\
                 Format your final answer exactly as:\n\
                 ## Conversation Summary\n\
                 **Task**: ...\n\
                 **Progress**: ...\n\
                 **Key Decisions**:\n- ...\n\
                 **Files Touched**:\n- ...\n\
                 **Current State**: ...\n\
                 **Open Issues**:\n- ...\n\n{prior}{grouped}"
            );
            if let Ok(summary) = summarizer.summarize(&prompt).await {
                if !summary.trim().is_empty() {
                    return summary.trim().to_string();
                }
            }
        }

        Self::heuristic_summary(messages, previous_summary, focus)
    }
}

#[async_trait]
impl AsyncCondensationStrategy for SummarizationStrategy {
    fn name(&self) -> &'static str {
        "summarization"
    }

    fn set_previous_summary(&mut self, summary: Option<String>) {
        self.previous_summary = summary;
    }

    async fn condense(&self, messages: &[Message], max_tokens: usize) -> Result<Vec<Message>> {
        let (system_messages, summarize, preserved) = Self::partition_messages(
            messages,
            self.preserve_recent,
            self.preserve_recent_turns,
            self.focus.as_deref(),
        );

        if summarize.is_empty() {
            return Ok(messages.to_vec());
        }

        let groups = Self::group_messages(&summarize, self.batch_size);
        let mut group_summaries = Vec::with_capacity(groups.len());
        for (idx, group) in groups.iter().enumerate() {
            group_summaries.push(self.summarize_group(group, idx + 1, groups.len()).await);
        }

        let summary_text = self.merge_summaries(&summarize, &group_summaries).await;
        let summary_message = Message::new(Role::System, summary_text);

        let mut result = system_messages;
        result.push(summary_message);
        result.extend(preserved);

        let total: usize = result.iter().map(estimate_tokens_for_message).sum();
        if total > max_tokens {
            tracing::warn!(
                total,
                max_tokens,
                "summarization output still exceeds token budget; returning fallback candidate"
            );
        }
        Ok(result)
    }
}

#[cfg(test)]
mod tests {
    use ava_types::{Message, Role, ToolCall, ToolResult};
    use serde_json::json;

    use super::*;

    #[tokio::test]
    async fn heuristic_summary_uses_required_sections() {
        let messages = vec![
            Message::new(
                Role::User,
                "Implement /compact for src/components/chat/MessageInput.tsx",
            ),
            Message::new(
                Role::Assistant,
                "I inspected the slash command handler and found a gap.",
            ),
        ];

        let summary = SummarizationStrategy::heuristic_summary(&messages, None, None);
        assert!(summary.contains("## Conversation Summary"));
        assert!(summary.contains("**Task**:"));
        assert!(summary.contains("**Progress**:"));
        assert!(summary.contains("**Files Touched**:"));
    }

    #[tokio::test]
    async fn condense_preserves_recent_turns_and_pinned_messages() {
        let strategy = SummarizationStrategy::new(None, 4, 2, 2, None);
        let mut pinned = Message::new(Role::Assistant, "Keep this architecture note");
        pinned.metadata = json!({ "pinned": true });

        let messages = vec![
            Message::new(Role::System, "system prompt"),
            Message::new(Role::User, "turn one"),
            pinned.clone(),
            Message::new(Role::User, "turn two"),
            Message::new(Role::Assistant, "recent response"),
            Message::new(Role::User, "turn three"),
            Message::new(Role::Assistant, "latest response"),
        ];

        let condensed = strategy.condense(&messages, 10_000).await.unwrap();
        assert_eq!(condensed[0].content, "system prompt");
        assert!(condensed
            .iter()
            .any(|message| message.content == pinned.content));
        assert!(condensed
            .iter()
            .any(|message| message.content == "turn three"));
        assert!(condensed
            .iter()
            .any(|message| message.content == "latest response"));
        assert!(condensed
            .iter()
            .any(|message| message.content.contains("## Conversation Summary")));
    }

    #[tokio::test]
    async fn condense_keeps_tool_pairs_when_focus_matches_tool_result() {
        let strategy = SummarizationStrategy::new(None, 3, 1, 1, Some("auth".to_string()));
        let mut assistant = Message::new(Role::Assistant, "Investigating auth flow");
        assistant.tool_calls.push(ToolCall {
            id: "call_1".to_string(),
            name: "read".to_string(),
            arguments: json!({ "path": "src/auth.rs" }),
        });
        let mut tool = Message::new(Role::Tool, "auth middleware implementation");
        tool.tool_call_id = Some("call_1".to_string());
        tool.tool_results.push(ToolResult {
            call_id: "call_1".to_string(),
            content: "auth token validation".to_string(),
            is_error: false,
        });

        let messages = vec![
            Message::new(Role::System, "system prompt"),
            Message::new(Role::User, "older task"),
            assistant.clone(),
            tool.clone(),
            Message::new(Role::User, "latest task"),
        ];

        let condensed = strategy.condense(&messages, 10_000).await.unwrap();
        assert!(condensed
            .iter()
            .any(|message| message.content == assistant.content));
        assert!(condensed
            .iter()
            .any(|message| message.content == tool.content));
    }
}
