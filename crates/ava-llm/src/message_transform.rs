//! Cross-provider message normalization.
//!
//! When switching providers mid-conversation (e.g., for cost-aware routing or
//! provider fallback), messages must be normalized so the target provider can
//! understand them. This module handles:
//!
//! - **Thinking block stripping**: Removes inline `<thinking>` / `<antThinking>` blocks
//!   when sending to providers that don't support them.
//! - **Tool call ID normalization**: Truncates/hashes overly long IDs (OpenAI can
//!   generate 450+ char IDs; Anthropic uses shorter ones).
//! - **Orphaned tool result repair**: Inserts synthetic tool calls when a tool result
//!   references a `tool_call_id` whose originating assistant message was removed
//!   (e.g., after context compaction).
//! - **Role normalization**: Ensures `Role::Tool` messages are present consistently.
//! - **Content format alignment**: Strips provider-specific content block markers.

use std::collections::HashSet;

use ava_types::{Message, Role, ToolCall};
use serde_json::json;

/// Maximum length for tool call IDs. IDs longer than this are truncated and
/// suffixed with a short hash to avoid collisions.
const MAX_TOOL_CALL_ID_LEN: usize = 64;

/// Known provider families for message normalization.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum ProviderKind {
    /// Anthropic Claude API (content block arrays, `tool_use` / `tool_result`).
    Anthropic,
    /// OpenAI / ChatGPT API (string content, `tool_calls` array, `tool` role).
    OpenAI,
    /// Google Gemini API (`parts` arrays, `model` role).
    Gemini,
    /// Ollama (OpenAI-compatible subset, limited tool support).
    Ollama,
    /// OpenRouter (proxy — normalizes to underlying model's format, treated as OpenAI).
    OpenRouter,
    /// GitHub Copilot (OpenAI-compatible).
    Copilot,
    /// Inception (OpenAI-compatible).
    Inception,
}

impl ProviderKind {
    /// Whether this provider natively understands thinking/reasoning blocks.
    pub fn supports_thinking_blocks(self) -> bool {
        matches!(self, Self::Anthropic | Self::Gemini)
    }

    /// Whether this provider uses the OpenAI-compatible message format.
    pub fn is_openai_compatible(self) -> bool {
        matches!(
            self,
            Self::OpenAI | Self::OpenRouter | Self::Copilot | Self::Ollama | Self::Inception
        )
    }

    /// Infer provider kind from a provider name string.
    pub fn from_provider_name(name: &str) -> Self {
        match name {
            "anthropic" | "alibaba" | "alibaba-cn" | "kimi-for-coding" | "minimax-coding-plan"
            | "minimax-cn-coding-plan" => Self::Anthropic,
            "openai" | "zai-coding-plan" | "zhipuai-coding-plan" => Self::OpenAI,
            "gemini" => Self::Gemini,
            "ollama" => Self::Ollama,
            "openrouter" => Self::OpenRouter,
            "copilot" => Self::Copilot,
            "inception" => Self::Inception,
            _ => Self::OpenAI, // safe default — most providers are OpenAI-compatible
        }
    }
}

/// Normalize a conversation's messages for the target provider.
///
/// This is the main entry point. Call this before sending messages to a
/// provider that differs from the one that originally generated them.
///
/// Transformations applied (in order):
/// 1. Strip inline thinking blocks (if target doesn't support them)
/// 2. Normalize tool call IDs (truncate long IDs)
/// 3. Repair orphaned tool results (insert synthetic tool calls)
pub fn normalize_messages(messages: &[Message], target: ProviderKind) -> Vec<Message> {
    let mut result: Vec<Message> = messages.to_vec();

    // 1. Strip thinking blocks from content if target doesn't support them
    if !target.supports_thinking_blocks() {
        for msg in &mut result {
            msg.content = strip_thinking_blocks(&msg.content);
        }
    }

    // 2. Normalize tool call IDs across all messages
    normalize_tool_call_ids(&mut result);

    // 3. Repair orphaned tool results
    repair_orphaned_tool_results(&mut result);

    result
}

// ── Thinking block stripping ──────────────────────────────────────────

/// Strip `<thinking>...</thinking>` and `<antThinking>...</antThinking>` blocks
/// from message content. These are inline reasoning markers that some providers
/// emit but others reject.
fn strip_thinking_blocks(content: &str) -> String {
    let mut result = content.to_string();

    // Strip <thinking>...</thinking> (including multiline)
    result = strip_tag_block(&result, "thinking");

    // Strip <antThinking>...</antThinking>
    result = strip_tag_block(&result, "antThinking");

    // Clean up any resulting double-newlines
    while result.contains("\n\n\n") {
        result = result.replace("\n\n\n", "\n\n");
    }

    result.trim().to_string()
}

/// Remove all occurrences of `<tag>...</tag>` from text (case-sensitive, supports multiline).
fn strip_tag_block(text: &str, tag: &str) -> String {
    let open = format!("<{tag}>");
    let close = format!("</{tag}>");
    let mut result = String::with_capacity(text.len());
    let mut remaining = text;

    loop {
        match remaining.find(&open) {
            Some(start) => {
                result.push_str(&remaining[..start]);
                match remaining[start..].find(&close) {
                    Some(end) => {
                        // Skip past the closing tag
                        remaining = &remaining[start + end + close.len()..];
                    }
                    None => {
                        // Unclosed tag — keep the rest as-is
                        result.push_str(&remaining[start..]);
                        break;
                    }
                }
            }
            None => {
                result.push_str(remaining);
                break;
            }
        }
    }

    result
}

// ── Tool call ID normalization ────────────────────────────────────────

/// Normalize all tool call IDs in the message list. Long IDs are truncated and
/// suffixed with a hash. The same mapping is applied consistently to both
/// `ToolCall.id` and `Message.tool_call_id` so references stay matched.
fn normalize_tool_call_ids(messages: &mut [Message]) {
    use std::collections::HashMap;

    let mut id_map: HashMap<String, String> = HashMap::new();

    // First pass: collect all tool call IDs that need normalization
    for msg in messages.iter() {
        for tc in &msg.tool_calls {
            if tc.id.len() > MAX_TOOL_CALL_ID_LEN {
                let normalized = truncate_id(&tc.id);
                id_map.insert(tc.id.clone(), normalized);
            }
        }
    }

    if id_map.is_empty() {
        return;
    }

    // Second pass: apply the mapping
    for msg in messages.iter_mut() {
        for tc in &mut msg.tool_calls {
            if let Some(new_id) = id_map.get(&tc.id) {
                tc.id = new_id.clone();
            }
        }
        if let Some(ref call_id) = msg.tool_call_id {
            if let Some(new_id) = id_map.get(call_id) {
                msg.tool_call_id = Some(new_id.clone());
            }
        }
    }
}

/// Truncate a tool call ID to `MAX_TOOL_CALL_ID_LEN`, appending a short hash
/// of the original to reduce collision risk.
fn truncate_id(id: &str) -> String {
    if id.len() <= MAX_TOOL_CALL_ID_LEN {
        return id.to_string();
    }

    // Simple FNV-1a hash for determinism without extra deps
    let hash = fnv1a_hash(id.as_bytes());
    let hash_suffix = format!("_{hash:08x}");
    let prefix_len = MAX_TOOL_CALL_ID_LEN - hash_suffix.len();
    format!("{}{}", &id[..prefix_len], hash_suffix)
}

/// FNV-1a hash (32-bit) — fast, deterministic, no external dependency.
fn fnv1a_hash(data: &[u8]) -> u32 {
    let mut hash: u32 = 0x811c_9dc5;
    for &byte in data {
        hash ^= byte as u32;
        hash = hash.wrapping_mul(0x0100_0193);
    }
    hash
}

// ── Orphaned tool result repair ───────────────────────────────────────

/// Find tool result messages (`Role::Tool`) whose `tool_call_id` doesn't
/// match any `ToolCall.id` in preceding assistant messages, and insert a
/// synthetic assistant tool call so the conversation stays valid.
fn repair_orphaned_tool_results(messages: &mut Vec<Message>) {
    // Collect all known tool call IDs from assistant messages
    let known_ids: HashSet<String> = messages
        .iter()
        .flat_map(|m| m.tool_calls.iter().map(|tc| tc.id.clone()))
        .collect();

    // Find orphaned tool results and their positions
    let mut insertions: Vec<(usize, Message)> = Vec::new();

    for (i, msg) in messages.iter().enumerate() {
        if msg.role != Role::Tool {
            continue;
        }
        let call_id = match &msg.tool_call_id {
            Some(id) if !id.is_empty() => id.clone(),
            _ => continue,
        };
        if known_ids.contains(&call_id) {
            continue;
        }

        // This tool result is orphaned — create a synthetic assistant message
        let synthetic = Message::new(Role::Assistant, "").with_tool_calls(vec![ToolCall {
            id: call_id.clone(),
            name: "unknown_tool".to_string(),
            arguments: json!({}),
        }]);
        insertions.push((i, synthetic));
    }

    // Insert in reverse order so indices stay valid
    for (pos, msg) in insertions.into_iter().rev() {
        messages.insert(pos, msg);
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use ava_types::{Message, Role, ToolCall};
    use serde_json::json;

    // ── Helpers ────────────────────────────────────────────────────────

    fn msg(role: Role, content: &str) -> Message {
        Message::new(role, content)
    }

    fn assistant_with_tool_calls(content: &str, calls: Vec<ToolCall>) -> Message {
        Message::new(Role::Assistant, content).with_tool_calls(calls)
    }

    fn tool_result(content: &str, call_id: &str) -> Message {
        Message::new(Role::Tool, content).with_tool_call_id(call_id)
    }

    fn tc(id: &str, name: &str) -> ToolCall {
        ToolCall {
            id: id.to_string(),
            name: name.to_string(),
            arguments: json!({}),
        }
    }

    // ── ProviderKind ───────────────────────────────────────────────────

    #[test]
    fn provider_kind_from_name() {
        assert_eq!(
            ProviderKind::from_provider_name("anthropic"),
            ProviderKind::Anthropic
        );
        assert_eq!(
            ProviderKind::from_provider_name("openai"),
            ProviderKind::OpenAI
        );
        assert_eq!(
            ProviderKind::from_provider_name("gemini"),
            ProviderKind::Gemini
        );
        assert_eq!(
            ProviderKind::from_provider_name("ollama"),
            ProviderKind::Ollama
        );
        assert_eq!(
            ProviderKind::from_provider_name("openrouter"),
            ProviderKind::OpenRouter
        );
        assert_eq!(
            ProviderKind::from_provider_name("copilot"),
            ProviderKind::Copilot
        );
        assert_eq!(
            ProviderKind::from_provider_name("inception"),
            ProviderKind::Inception
        );
        // Anthropic-compatible coding plan providers
        assert_eq!(
            ProviderKind::from_provider_name("alibaba"),
            ProviderKind::Anthropic
        );
        assert_eq!(
            ProviderKind::from_provider_name("kimi-for-coding"),
            ProviderKind::Anthropic
        );
        // OpenAI-compatible coding plan providers
        assert_eq!(
            ProviderKind::from_provider_name("zai-coding-plan"),
            ProviderKind::OpenAI
        );
        // Unknown defaults to OpenAI
        assert_eq!(
            ProviderKind::from_provider_name("some-future-provider"),
            ProviderKind::OpenAI
        );
    }

    #[test]
    fn provider_kind_thinking_support() {
        assert!(ProviderKind::Anthropic.supports_thinking_blocks());
        assert!(ProviderKind::Gemini.supports_thinking_blocks());
        assert!(!ProviderKind::OpenAI.supports_thinking_blocks());
        assert!(!ProviderKind::Ollama.supports_thinking_blocks());
        assert!(!ProviderKind::OpenRouter.supports_thinking_blocks());
        assert!(!ProviderKind::Copilot.supports_thinking_blocks());
        assert!(!ProviderKind::Inception.supports_thinking_blocks());
    }

    #[test]
    fn provider_kind_openai_compatible() {
        assert!(!ProviderKind::Anthropic.is_openai_compatible());
        assert!(!ProviderKind::Gemini.is_openai_compatible());
        assert!(ProviderKind::OpenAI.is_openai_compatible());
        assert!(ProviderKind::Ollama.is_openai_compatible());
        assert!(ProviderKind::OpenRouter.is_openai_compatible());
        assert!(ProviderKind::Copilot.is_openai_compatible());
        assert!(ProviderKind::Inception.is_openai_compatible());
    }

    // ── Thinking block stripping ───────────────────────────────────────

    #[test]
    fn strip_thinking_simple() {
        let input = "Before <thinking>internal reasoning here</thinking> After";
        let output = strip_thinking_blocks(input);
        assert_eq!(output, "Before  After");
    }

    #[test]
    fn strip_thinking_multiline() {
        let input = "Start\n<thinking>\nLine 1\nLine 2\n</thinking>\nEnd";
        let output = strip_thinking_blocks(input);
        assert_eq!(output, "Start\n\nEnd");
    }

    #[test]
    fn strip_ant_thinking() {
        let input = "Hello <antThinking>reasoning</antThinking> world";
        let output = strip_thinking_blocks(input);
        assert_eq!(output, "Hello  world");
    }

    #[test]
    fn strip_thinking_both_types() {
        let input =
            "<thinking>thought 1</thinking> middle <antThinking>thought 2</antThinking> end";
        let output = strip_thinking_blocks(input);
        assert_eq!(output, "middle  end");
    }

    #[test]
    fn strip_thinking_no_blocks() {
        let input = "No thinking blocks here";
        let output = strip_thinking_blocks(input);
        assert_eq!(output, "No thinking blocks here");
    }

    #[test]
    fn strip_thinking_empty_content() {
        let output = strip_thinking_blocks("");
        assert_eq!(output, "");
    }

    #[test]
    fn strip_thinking_unclosed_tag_preserved() {
        let input = "Start <thinking>unclosed reasoning";
        let output = strip_thinking_blocks(input);
        assert_eq!(output, "Start <thinking>unclosed reasoning");
    }

    #[test]
    fn strip_thinking_multiple_blocks() {
        let input = "<thinking>a</thinking> X <thinking>b</thinking> Y";
        let output = strip_thinking_blocks(input);
        assert_eq!(output, "X  Y");
    }

    #[test]
    fn normalize_strips_thinking_for_openai() {
        let messages = vec![msg(
            Role::Assistant,
            "Hello <thinking>internal</thinking> world",
        )];
        let result = normalize_messages(&messages, ProviderKind::OpenAI);
        assert_eq!(result[0].content, "Hello  world");
    }

    #[test]
    fn normalize_preserves_thinking_for_anthropic() {
        let content = "Hello <thinking>internal</thinking> world";
        let messages = vec![msg(Role::Assistant, content)];
        let result = normalize_messages(&messages, ProviderKind::Anthropic);
        assert_eq!(result[0].content, content);
    }

    #[test]
    fn normalize_preserves_thinking_for_gemini() {
        let content = "Hello <thinking>internal</thinking> world";
        let messages = vec![msg(Role::Assistant, content)];
        let result = normalize_messages(&messages, ProviderKind::Gemini);
        assert_eq!(result[0].content, content);
    }

    // ── Tool call ID normalization ─────────────────────────────────────

    #[test]
    fn short_ids_unchanged() {
        let messages = vec![
            assistant_with_tool_calls("", vec![tc("call_abc", "read_file")]),
            tool_result("content", "call_abc"),
        ];
        let result = normalize_messages(&messages, ProviderKind::OpenAI);
        assert_eq!(result[0].tool_calls[0].id, "call_abc");
        assert_eq!(result[1].tool_call_id.as_deref(), Some("call_abc"));
    }

    #[test]
    fn long_ids_truncated() {
        let long_id = "a".repeat(200);
        let messages = vec![
            assistant_with_tool_calls("", vec![tc(&long_id, "read_file")]),
            tool_result("content", &long_id),
        ];
        let result = normalize_messages(&messages, ProviderKind::Anthropic);
        let new_id = &result[0].tool_calls[0].id;
        assert!(new_id.len() <= MAX_TOOL_CALL_ID_LEN);
        // The tool result should have the same normalized ID
        assert_eq!(result[1].tool_call_id.as_deref(), Some(new_id.as_str()));
    }

    #[test]
    fn truncated_id_is_deterministic() {
        let long_id = "call_".to_string() + &"x".repeat(500);
        let id1 = truncate_id(&long_id);
        let id2 = truncate_id(&long_id);
        assert_eq!(id1, id2);
    }

    #[test]
    fn different_long_ids_produce_different_truncations() {
        let id_a = "a".repeat(200);
        let id_b = "b".repeat(200);
        let trunc_a = truncate_id(&id_a);
        let trunc_b = truncate_id(&id_b);
        assert_ne!(trunc_a, trunc_b);
    }

    #[test]
    fn multiple_long_ids_all_normalized() {
        let id1 = "x".repeat(100);
        let id2 = "y".repeat(150);
        let messages = vec![
            assistant_with_tool_calls(
                "",
                vec![tc(&id1, "read_file"), tc(&id2, "write_file")],
            ),
            tool_result("result1", &id1),
            tool_result("result2", &id2),
        ];
        let result = normalize_messages(&messages, ProviderKind::OpenAI);
        let norm_id1 = &result[0].tool_calls[0].id;
        let norm_id2 = &result[0].tool_calls[1].id;
        assert!(norm_id1.len() <= MAX_TOOL_CALL_ID_LEN);
        assert!(norm_id2.len() <= MAX_TOOL_CALL_ID_LEN);
        assert_eq!(
            result[1].tool_call_id.as_deref(),
            Some(norm_id1.as_str())
        );
        assert_eq!(
            result[2].tool_call_id.as_deref(),
            Some(norm_id2.as_str())
        );
    }

    // ── Orphaned tool result repair ────────────────────────────────────

    #[test]
    fn orphaned_tool_result_gets_synthetic_call() {
        let messages = vec![
            msg(Role::User, "do something"),
            // No assistant message with tool_call_id "call_orphan"
            tool_result("some result", "call_orphan"),
        ];
        let result = normalize_messages(&messages, ProviderKind::OpenAI);
        // Should have 3 messages: user, synthetic assistant, tool result
        assert_eq!(result.len(), 3);
        assert_eq!(result[1].role, Role::Assistant);
        assert_eq!(result[1].tool_calls.len(), 1);
        assert_eq!(result[1].tool_calls[0].id, "call_orphan");
        assert_eq!(result[2].role, Role::Tool);
    }

    #[test]
    fn non_orphaned_tool_result_unchanged() {
        let messages = vec![
            msg(Role::User, "do something"),
            assistant_with_tool_calls("", vec![tc("call_1", "read_file")]),
            tool_result("content", "call_1"),
        ];
        let result = normalize_messages(&messages, ProviderKind::OpenAI);
        // No synthetic messages should be inserted
        assert_eq!(result.len(), 3);
        assert_eq!(result[0].role, Role::User);
        assert_eq!(result[1].role, Role::Assistant);
        assert_eq!(result[2].role, Role::Tool);
    }

    #[test]
    fn multiple_orphaned_results_all_repaired() {
        let messages = vec![
            msg(Role::User, "do things"),
            tool_result("result A", "orphan_a"),
            tool_result("result B", "orphan_b"),
        ];
        let result = normalize_messages(&messages, ProviderKind::Anthropic);
        // Should have 5 messages: user, synth_a, tool_a, synth_b, tool_b
        assert_eq!(result.len(), 5);
        assert_eq!(result[1].role, Role::Assistant);
        assert_eq!(result[1].tool_calls[0].id, "orphan_a");
        assert_eq!(result[2].role, Role::Tool);
        assert_eq!(result[3].role, Role::Assistant);
        assert_eq!(result[3].tool_calls[0].id, "orphan_b");
        assert_eq!(result[4].role, Role::Tool);
    }

    #[test]
    fn tool_result_without_call_id_not_touched() {
        let messages = vec![
            msg(Role::User, "something"),
            msg(Role::Tool, "bare result"),
        ];
        let result = normalize_messages(&messages, ProviderKind::OpenAI);
        assert_eq!(result.len(), 2);
    }

    // ── Round-trip tests ───────────────────────────────────────────────

    #[test]
    fn round_trip_openai_to_anthropic() {
        let long_id = "call_".to_string() + &"z".repeat(500);
        let messages = vec![
            msg(Role::System, "You are helpful"),
            msg(Role::User, "Read a file"),
            assistant_with_tool_calls(
                "<thinking>Let me read it</thinking>I'll read the file",
                vec![tc(&long_id, "read_file")],
            ),
            tool_result("file contents here", &long_id),
            msg(Role::Assistant, "Here are the contents."),
        ];

        // Normalize for OpenAI
        let for_openai = normalize_messages(&messages, ProviderKind::OpenAI);
        // Thinking should be stripped
        assert!(!for_openai[2].content.contains("<thinking>"));
        assert!(for_openai[2].content.contains("read the file"));
        // IDs should be normalized
        let openai_id = &for_openai[2].tool_calls[0].id;
        assert!(openai_id.len() <= MAX_TOOL_CALL_ID_LEN);

        // Now normalize the OpenAI-normalized messages for Anthropic
        let for_anthropic = normalize_messages(&for_openai, ProviderKind::Anthropic);
        // Should still have same number of messages
        assert_eq!(for_anthropic.len(), for_openai.len());
        // IDs should remain consistent (already short enough)
        assert_eq!(
            for_anthropic[2].tool_calls[0].id,
            for_openai[2].tool_calls[0].id
        );
        assert_eq!(for_anthropic[3].tool_call_id, for_openai[3].tool_call_id);
    }

    #[test]
    fn normalize_idempotent() {
        let messages = vec![
            msg(Role::User, "hello"),
            assistant_with_tool_calls(
                "I'll help <thinking>plan</thinking>",
                vec![tc("call_short", "read")],
            ),
            tool_result("data", "call_short"),
            msg(Role::Assistant, "Done!"),
        ];

        let first = normalize_messages(&messages, ProviderKind::OpenAI);
        let second = normalize_messages(&first, ProviderKind::OpenAI);

        assert_eq!(first.len(), second.len());
        for (a, b) in first.iter().zip(second.iter()) {
            assert_eq!(a.content, b.content);
            assert_eq!(a.role, b.role);
            assert_eq!(a.tool_call_id, b.tool_call_id);
            assert_eq!(a.tool_calls.len(), b.tool_calls.len());
            for (ta, tb) in a.tool_calls.iter().zip(b.tool_calls.iter()) {
                assert_eq!(ta.id, tb.id);
                assert_eq!(ta.name, tb.name);
            }
        }
    }

    // ── Edge cases ─────────────────────────────────────────────────────

    #[test]
    fn empty_messages() {
        let result = normalize_messages(&[], ProviderKind::OpenAI);
        assert!(result.is_empty());
    }

    #[test]
    fn single_user_message() {
        let messages = vec![msg(Role::User, "hello")];
        let result = normalize_messages(&messages, ProviderKind::Anthropic);
        assert_eq!(result.len(), 1);
        assert_eq!(result[0].content, "hello");
    }

    #[test]
    fn system_messages_preserved() {
        let messages = vec![msg(Role::System, "You are a helpful assistant")];
        let result = normalize_messages(&messages, ProviderKind::OpenAI);
        assert_eq!(result.len(), 1);
        assert_eq!(result[0].role, Role::System);
        assert_eq!(result[0].content, "You are a helpful assistant");
    }

    #[test]
    fn all_transformations_combined() {
        let long_id = "x".repeat(200);
        let messages = vec![
            msg(Role::System, "system prompt"),
            msg(Role::User, "do something"),
            assistant_with_tool_calls(
                "<thinking>plan</thinking>Executing",
                vec![tc(&long_id, "bash")],
            ),
            tool_result("output", &long_id),
            // Orphaned tool result (no matching assistant tool call)
            tool_result("orphaned output", "missing_call"),
            msg(Role::Assistant, "All done."),
        ];

        let result = normalize_messages(&messages, ProviderKind::OpenAI);

        // Thinking stripped
        assert!(!result[2].content.contains("<thinking>"));
        assert!(result[2].content.contains("Executing"));

        // Long ID truncated
        assert!(result[2].tool_calls[0].id.len() <= MAX_TOOL_CALL_ID_LEN);

        // Orphaned tool result repaired (synthetic assistant inserted before it)
        let orphan_idx = result
            .iter()
            .position(|m| {
                m.role == Role::Tool && m.tool_call_id.as_deref() == Some("missing_call")
            })
            .unwrap();
        assert_eq!(result[orphan_idx - 1].role, Role::Assistant);
        assert_eq!(result[orphan_idx - 1].tool_calls[0].id, "missing_call");
    }

    // ── Internal function tests ────────────────────────────────────────

    #[test]
    fn fnv1a_hash_deterministic() {
        let h1 = fnv1a_hash(b"hello world");
        let h2 = fnv1a_hash(b"hello world");
        assert_eq!(h1, h2);
    }

    #[test]
    fn fnv1a_hash_different_inputs() {
        let h1 = fnv1a_hash(b"abc");
        let h2 = fnv1a_hash(b"def");
        assert_ne!(h1, h2);
    }

    #[test]
    fn truncate_id_short_unchanged() {
        let id = "call_abc123";
        assert_eq!(truncate_id(id), id);
    }

    #[test]
    fn truncate_id_exact_limit() {
        let id = "a".repeat(MAX_TOOL_CALL_ID_LEN);
        assert_eq!(truncate_id(&id), id);
    }

    #[test]
    fn truncate_id_over_limit() {
        let id = "a".repeat(MAX_TOOL_CALL_ID_LEN + 1);
        let truncated = truncate_id(&id);
        assert!(truncated.len() <= MAX_TOOL_CALL_ID_LEN);
        assert!(truncated.contains('_')); // hash suffix
    }

    #[test]
    fn strip_tag_block_basic() {
        assert_eq!(
            strip_tag_block("before <x>middle</x> after", "x"),
            "before  after"
        );
    }

    #[test]
    fn strip_tag_block_no_match() {
        assert_eq!(
            strip_tag_block("no tags here", "x"),
            "no tags here"
        );
    }

    #[test]
    fn strip_tag_block_empty() {
        assert_eq!(strip_tag_block("", "x"), "");
    }
}
