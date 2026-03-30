use ava_types::Message;

#[derive(Debug, Clone)]
pub struct ContextChunk {
    pub messages: Vec<Message>,
    pub estimated_tokens: usize,
}

#[derive(Debug, Clone)]
pub struct CondensationResult {
    pub messages: Vec<Message>,
    pub estimated_tokens: usize,
    pub strategy: String,
    /// Messages that were compacted (removed from the agent context).
    /// These have `agent_visible = false` and `original_content` set so
    /// the UI can still display them dimmed/collapsed.
    pub compacted_messages: Vec<Message>,
}

#[derive(Debug, Clone)]
pub struct CompactionReport {
    pub tokens_before: usize,
    pub tokens_after: usize,
    pub tokens_saved: usize,
    pub messages_before: usize,
    pub messages_after: usize,
    pub strategy: String,
    pub summary: Option<String>,
}

#[derive(Debug, Clone)]
pub struct CondenserConfig {
    pub max_tokens: usize,
    pub target_tokens: usize,
    pub max_tool_content_chars: usize,
    /// Keep the last N messages intact during summarization (default 4).
    pub preserve_recent_messages: usize,
    /// Keep the last N user turns intact during summarization (default 2).
    pub preserve_recent_turns: usize,
    /// Use LLM-based summarization when available (default true).
    pub enable_summarization: bool,
    /// Number of oldest messages to summarize per batch (default 20).
    pub summarization_batch_size: usize,
    /// Trigger compaction at this fraction of max_tokens (default 0.8).
    pub compaction_threshold_pct: f32,
    /// Optional focus hint for manual compaction.
    pub focus: Option<String>,
}

impl Default for CondenserConfig {
    fn default() -> Self {
        Self {
            max_tokens: 16_000,
            target_tokens: 12_000,
            max_tool_content_chars: 2_000,
            preserve_recent_messages: 4,
            preserve_recent_turns: 2,
            enable_summarization: true,
            summarization_batch_size: 20,
            compaction_threshold_pct: 0.8,
            focus: None,
        }
    }
}
