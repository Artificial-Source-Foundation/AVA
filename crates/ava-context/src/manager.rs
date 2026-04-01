use ava_types::{Message, Role, ToolResult};

use crate::condenser::{create_condenser, Condenser, HybridCondenser};
use crate::content_budget::ContentReplacementBudget;
use crate::pruner::prune_old_tool_outputs;
use crate::token_tracker::TokenTracker;
use crate::types::{CompactionReport, CondenserConfig};
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
    /// Messages that have been compacted (hidden from agent, visible to user).
    /// Accumulated across compaction rounds so the UI can display the full history.
    compacted_messages: Vec<Message>,
    /// Latest compaction report for UI notifications / metrics.
    last_compaction_report: Option<CompactionReport>,
    /// When true, conversation repair should run before the next LLM request.
    needs_repair: bool,
    /// Content replacement budget — tracks tool output sizes and evicts when over budget.
    content_budget: ContentReplacementBudget,
    /// Monotonically increasing turn counter for content budget tracking.
    turn_counter: usize,
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
            compacted_messages: Vec::new(),
            last_compaction_report: None,
            needs_repair: false,
            content_budget: ContentReplacementBudget::default(),
            turn_counter: 0,
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
            compacted_messages: Vec::new(),
            last_compaction_report: None,
            needs_repair: false,
            content_budget: ContentReplacementBudget::default(),
            turn_counter: 0,
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
        if !self.messages.is_empty() {
            self.needs_repair = true;
        }
    }

    pub fn add_message(&mut self, message: Message) {
        self.tracker.add_message(&message);
        if matches!(message.role, Role::Assistant | Role::Tool | Role::User) {
            self.needs_repair = true;
        }
        self.messages.push(message);
    }

    pub fn add_tool_result(&mut self, result: ToolResult) {
        self.turn_counter += 1;
        self.content_budget.record_output(
            &result.call_id,
            "tool",
            &result.content,
            self.turn_counter,
        );
        let message =
            Message::new(Role::Tool, result.content.clone()).with_tool_results(vec![result]);
        self.add_message(message);
    }

    pub fn get_messages(&self) -> &[Message] {
        &self.messages
    }

    /// Replace the entire message list (e.g. after conversation repair).
    /// Recalculates token counts from scratch.
    pub fn replace_messages(&mut self, messages: Vec<Message>) {
        self.tracker = TokenTracker::new(self.token_limit);
        for msg in &messages {
            self.tracker.add_message(msg);
        }
        self.messages = messages;
        self.needs_repair = false;
    }

    pub fn needs_repair(&self) -> bool {
        self.needs_repair
    }

    pub fn mark_needs_repair(&mut self) {
        self.needs_repair = true;
    }

    pub fn clear_needs_repair(&mut self) {
        self.needs_repair = false;
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
        // First pass: content budget eviction (character-based).
        let budget_evicted = if self.content_budget.is_over_budget() {
            let evicted = self.content_budget.evict_oldest(&mut self.messages);
            if evicted > 0 {
                self.tracker.reset();
                self.tracker.add_messages(&self.messages);
                self.needs_repair = true;
            }
            evicted
        } else {
            0
        };

        // Second pass: token-based pruning of old tool outputs.
        // Protect the most recent 60% of the token limit.
        let protected = (self.token_limit as f32 * 0.6) as usize;
        let pruned = prune_old_tool_outputs(&mut self.messages, protected);
        if pruned > 0 {
            self.tracker.reset();
            self.tracker.add_messages(&self.messages);
            self.needs_repair = true;
        }
        budget_evicted + pruned
    }

    /// Get a reference to the content replacement budget.
    pub fn content_budget(&self) -> &ContentReplacementBudget {
        &self.content_budget
    }

    /// Synchronous compaction — only works when using a sync Condenser.
    /// For hybrid condensers, use `compact_async()`.
    pub fn compact(&mut self) -> Result<()> {
        let before_tokens = self.token_count();
        let before_messages = self.messages.len();
        match &mut self.condenser {
            CondenserKind::Sync(condenser) => {
                let condensed = condenser.condense(&self.messages)?;
                let strategy = condensed.strategy.clone();
                self.compacted_messages.extend(condensed.compacted_messages);
                self.messages = condensed.messages;
                self.tracker.reset();
                self.tracker.add_messages(&self.messages);
                self.last_compaction_report = Some(CompactionReport {
                    tokens_before: before_tokens,
                    tokens_after: self.token_count(),
                    tokens_saved: before_tokens.saturating_sub(self.token_count()),
                    messages_before: before_messages,
                    messages_after: self.messages.len(),
                    strategy,
                    summary: self.last_summary.clone(),
                });
                self.needs_repair = true;
                Ok(())
            }
            CondenserKind::Hybrid(_) => {
                // Caller should use compact_async() for hybrid condensers.
                // Fall back to a basic sliding window to avoid panicking.
                use crate::strategies::{CondensationStrategy, SlidingWindowStrategy};
                let original = self.messages.clone();
                let target = (self.token_limit as f32 * 0.75) as usize;
                let condensed = SlidingWindowStrategy.condense(&self.messages, target)?;
                // Mark dropped messages as compacted
                let compacted =
                    crate::condenser::mark_compacted_messages_pub(&original, &condensed);
                self.compacted_messages.extend(compacted);
                self.messages = condensed;
                self.tracker.reset();
                self.tracker.add_messages(&self.messages);
                self.last_compaction_report = Some(CompactionReport {
                    tokens_before: before_tokens,
                    tokens_after: self.token_count(),
                    tokens_saved: before_tokens.saturating_sub(self.token_count()),
                    messages_before: before_messages,
                    messages_after: self.messages.len(),
                    strategy: "sliding_window".to_string(),
                    summary: self.last_summary.clone(),
                });
                self.needs_repair = true;
                Ok(())
            }
        }
    }

    /// Async compaction — uses the hybrid condenser pipeline.
    /// Stores the compaction summary for iterative use in subsequent compactions.
    pub async fn compact_async(&mut self) -> Result<()> {
        let before_tokens = self.token_count();
        let before_messages = self.messages.len();
        // Feed previous summary into the hybrid condenser's summarization strategy
        if let CondenserKind::Hybrid(condenser) = &mut self.condenser {
            condenser.set_previous_summary(self.last_summary.clone());
        }

        let strategy = match &mut self.condenser {
            CondenserKind::Sync(condenser) => {
                let condensed = condenser.condense(&self.messages)?;
                let strategy = condensed.strategy.clone();
                self.compacted_messages.extend(condensed.compacted_messages);
                self.messages = condensed.messages;
                strategy
            }
            CondenserKind::Hybrid(condenser) => {
                let condensed = condenser.condense(&self.messages).await?;
                let strategy = condensed.strategy.clone();
                self.compacted_messages.extend(condensed.compacted_messages);
                self.messages = condensed.messages;
                strategy
            }
        };

        // Extract the latest summary from system messages (the compaction summary
        // is inserted as a system message containing "[Summary" or "[Updated summary")
        self.last_summary = self
            .messages
            .iter()
            .rev()
            .filter(|m| m.role == Role::System)
            .find(|m| {
                m.content.starts_with("[Summary of")
                    || m.content.starts_with("[Updated summary")
                    || m.content.starts_with("## Conversation Summary")
                    || m.content.contains("Files read:")
                    || m.content.contains("Files modified:")
            })
            .map(|m| m.content.clone());

        tracing::debug!(
            messages = self.messages.len(),
            compacted = self.compacted_messages.len(),
            has_summary = self.last_summary.is_some(),
            summary_chars = self
                .last_summary
                .as_ref()
                .map_or(0, |summary| summary.len()),
            "context compaction state updated"
        );

        self.tracker.reset();
        self.tracker.add_messages(&self.messages);
        self.last_compaction_report = Some(CompactionReport {
            tokens_before: before_tokens,
            tokens_after: self.token_count(),
            tokens_saved: before_tokens.saturating_sub(self.token_count()),
            messages_before: before_messages,
            messages_after: self.messages.len(),
            strategy,
            summary: self.last_summary.clone(),
        });
        self.needs_repair = true;
        Ok(())
    }

    /// Get the last compaction summary (for external inspection or persistence).
    pub fn last_summary(&self) -> Option<&str> {
        self.last_summary.as_deref()
    }

    pub fn last_compaction_report(&self) -> Option<&CompactionReport> {
        self.last_compaction_report.as_ref()
    }

    pub fn get_system_message(&self) -> Option<&Message> {
        self.messages
            .iter()
            .find(|message| message.role == Role::System)
    }

    /// Return only the messages that should be sent to the LLM (agent-visible).
    /// This filters out messages that have been compacted.
    pub fn get_agent_visible_messages(&self) -> Vec<&Message> {
        self.messages.iter().filter(|m| m.agent_visible).collect()
    }

    /// Return all messages including compacted ones, for UI display.
    /// Compacted messages (from previous compaction rounds) come first,
    /// followed by the current context messages. Compacted messages have
    /// `agent_visible = false` and `is_compacted() = true`.
    pub fn get_all_messages_for_ui(&self) -> Vec<&Message> {
        let mut all: Vec<&Message> = self.compacted_messages.iter().collect();
        all.extend(self.messages.iter());
        // Sort by timestamp to maintain chronological order
        all.sort_by_key(|m| m.timestamp);
        all
    }

    /// Get the compacted messages from previous compaction rounds.
    pub fn compacted_messages(&self) -> &[Message] {
        &self.compacted_messages
    }
}
