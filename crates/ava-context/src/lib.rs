//! AVA Context - token tracking and condensation orchestration.

pub mod condenser;
pub mod error;
pub mod focus;
pub mod history_processor;
pub mod manager;
pub mod pruner;
pub mod remote_compaction;
pub mod strategies;
pub mod token_tracker;
pub mod types;

pub use condenser::{
    create_condenser, create_hybrid_condenser, create_hybrid_condenser_with_relevance, Condenser,
    HybridCondenser,
};
pub use error::{ContextError, Result};
pub use focus::{AccessKind, FocusChain, FocusEntry};
pub use manager::ContextManager;
pub use pruner::prune_old_tool_outputs;
pub use strategies::{
    AsyncCondensationStrategy, CondensationStrategy, RelevanceStrategy, SlidingWindowStrategy,
    SummarizationStrategy, Summarizer, ToolTruncationStrategy,
};
pub use token_tracker::{estimate_tokens, estimate_tokens_for_message, TokenTracker};
pub use types::{CondensationResult, CondenserConfig, ContextChunk};
