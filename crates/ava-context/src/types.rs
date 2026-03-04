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
}

impl Default for CondenserConfig {
    fn default() -> Self {
        Self {
            max_tokens: 16_000,
            target_tokens: 12_000,
            max_tool_content_chars: 2_000,
        }
    }
}
