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
}

#[derive(Debug, Clone)]
pub struct CondenserConfig {
    pub max_tokens: usize,
    pub target_tokens: usize,
    pub max_tool_content_chars: usize,
    /// Keep the last N messages intact during summarization (default 10).
    pub preserve_recent_messages: usize,
    /// Use LLM-based summarization when available (default true).
    pub enable_summarization: bool,
    /// Number of oldest messages to summarize per batch (default 20).
    pub summarization_batch_size: usize,
    /// Trigger compaction at this fraction of max_tokens (default 0.8).
    pub compaction_threshold_pct: f32,
}

impl Default for CondenserConfig {
    fn default() -> Self {
        Self {
            max_tokens: 16_000,
            target_tokens: 12_000,
            max_tool_content_chars: 2_000,
            preserve_recent_messages: 10,
            enable_summarization: true,
            summarization_batch_size: 20,
            compaction_threshold_pct: 0.8,
        }
    }
}
