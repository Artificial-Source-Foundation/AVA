use ava_types::{Message, Role, ToolResult};

use crate::condenser::{create_condenser, Condenser, HybridCondenser};
use crate::pruner::prune_old_tool_outputs;
use crate::token_tracker::TokenTracker;
use crate::types::CondenserConfig;
use crate::Result;

enum CondenserKind {
    Sync(Condenser),
    Hybrid(HybridCondenser),
}

pub struct ContextManager {
    messages: Vec<Message>,
    token_limit: usize,
    compaction_threshold_pct: f32,
    tracker: TokenTracker,
    condenser: CondenserKind,
    /// Latest compaction summary for iterative summarization (BG-7).
    last_summary: Option<String>,
}

impl ContextManager {
    pub fn new(token_limit: usize) -> Self {
        Self {
            messages: Vec::new(),
            token_limit,
            compaction_threshold_pct: 0.8,
            tracker: TokenTracker::new(token_limit),
            condenser: CondenserKind::Sync(create_condenser(token_limit)),
            last_summary: None,
        }
    }

    pub fn new_with_condenser(config: CondenserConfig, condenser: HybridCondenser) -> Self {
        let threshold = config.compaction_threshold_pct;
        Self {
            messages: Vec::new(),
            token_limit: config.max_tokens,
            compaction_threshold_pct: threshold,
            tracker: TokenTracker::new(config.max_tokens),
            condenser: CondenserKind::Hybrid(condenser),
            last_summary: None,
        }
    }

    /// Load initial conversation history into the context.
    /// Messages are added in order and tracked for token counts.
    /// Call this before `inject_system_prompt` / adding the goal.
    pub fn load_history(&mut self, messages: Vec<Message>) {
        for msg in messages {
            self.tracker.add_message(&msg);
            self.messages.push(msg);
        }
    }

    pub fn add_message(&mut self, message: Message) {
        self.tracker.add_message(&message);
        self.messages.push(message);
    }

    pub fn add_tool_result(&mut self, result: ToolResult) {
        let message =
            Message::new(Role::Tool, result.content.clone()).with_tool_results(vec![result]);
        self.add_message(message);
    }

    pub fn get_messages(&self) -> &[Message] {
        &self.messages
    }

    pub fn token_count(&self) -> usize {
        self.tracker.current_tokens
    }

    pub fn should_compact(&self) -> bool {
        let threshold = (self.token_limit as f32 * self.compaction_threshold_pct) as usize;
        self.token_count() > threshold
    }

    /// Lightweight pruning pass: replaces old tool outputs with a short summary.
    /// Returns the number of messages pruned.
    ///
    /// Call this before full compaction — if pruning brings usage below the
    /// threshold, the expensive LLM compaction can be skipped entirely.
    pub fn try_prune(&mut self) -> usize {
        // Protect the most recent 60% of the token limit.
        let protected = (self.token_limit as f32 * 0.6) as usize;
        let pruned = prune_old_tool_outputs(&mut self.messages, protected);
        if pruned > 0 {
            self.tracker.reset();
            self.tracker.add_messages(&self.messages);
        }
        pruned
    }

    /// Synchronous compaction — only works when using a sync Condenser.
    /// For hybrid condensers, use `compact_async()`.
    pub fn compact(&mut self) -> Result<()> {
        match &mut self.condenser {
            CondenserKind::Sync(condenser) => {
                let condensed = condenser.condense(&self.messages)?;
                self.messages = condensed.messages;
                self.tracker.reset();
                self.tracker.add_messages(&self.messages);
                Ok(())
            }
            CondenserKind::Hybrid(_) => {
                // Caller should use compact_async() for hybrid condensers.
                // Fall back to a basic sliding window to avoid panicking.
                use crate::strategies::{CondensationStrategy, SlidingWindowStrategy};
                let target = (self.token_limit as f32 * 0.75) as usize;
                let condensed = SlidingWindowStrategy.condense(&self.messages, target)?;
                self.messages = condensed;
                self.tracker.reset();
                self.tracker.add_messages(&self.messages);
                Ok(())
            }
        }
    }

    /// Async compaction — uses the hybrid condenser pipeline.
    /// Stores the compaction summary for iterative use in subsequent compactions.
    pub async fn compact_async(&mut self) -> Result<()> {
        // Feed previous summary into the hybrid condenser's summarization strategy
        if let CondenserKind::Hybrid(condenser) = &mut self.condenser {
            condenser.set_previous_summary(self.last_summary.clone());
        }

        match &mut self.condenser {
            CondenserKind::Sync(condenser) => {
                let condensed = condenser.condense(&self.messages)?;
                self.messages = condensed.messages;
            }
            CondenserKind::Hybrid(condenser) => {
                let condensed = condenser.condense(&self.messages).await?;
                self.messages = condensed.messages;
            }
        }

        // Extract the latest summary from system messages (the compaction summary
        // is inserted as a system message containing "[Summary" or "[Updated summary")
        self.last_summary = self
            .messages
            .iter()
            .filter(|m| m.role == Role::System)
            .find(|m| {
                m.content.starts_with("[Summary of")
                    || m.content.starts_with("[Updated summary")
                    || m.content.contains("Files read:")
                    || m.content.contains("Files modified:")
            })
            .map(|m| m.content.clone());

        self.tracker.reset();
        self.tracker.add_messages(&self.messages);
        Ok(())
    }

    /// Get the last compaction summary (for external inspection or persistence).
    pub fn last_summary(&self) -> Option<&str> {
        self.last_summary.as_deref()
    }

    pub fn get_system_message(&self) -> Option<&Message> {
        self.messages
            .iter()
            .find(|message| message.role == Role::System)
    }
}
