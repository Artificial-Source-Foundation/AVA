/**
 * @estela/core Context Module
 * Token tracking, compaction strategies, and context window management
 */

// Compactor
export {
  Compactor,
  type CompactorConfig,
  createAggressiveCompactor,
  createAutoCompactor,
  createCompactor,
} from './compactor.js'
// Strategies
export {
  buildSummaryTree,
  clearHierarchicalCache,
  createHierarchical,
  createSlidingWindow,
  createSummarize,
  createToolTruncation,
  createVerifiedSummarize,
  DEFAULT_PRESERVE_FRACTION,
  extractStateSnapshot,
  extractSummary,
  findAllSplitPoints,
  findSafeSplitPoint,
  findSizeSplitPoint,
  getContentSizeUpTo,
  getStateSnapshotPrompt,
  getSummarizationPrompt,
  getVerificationPrompt,
  type HierarchicalConfig,
  hierarchical,
  MIN_PRESERVE_MESSAGES,
  type SlidingWindowOptions,
  STATE_SNAPSHOT_CLOSE_TAG,
  STATE_SNAPSHOT_TAG,
  selectLevel,
  slidingWindow,
  summarize,
  type ToolTruncationConfig,
  toolTruncation,
  truncateContent,
  type VerifiedSummarizeConfig,
} from './strategies/index.js'
// Token Tracking
export {
  ContextTracker,
  countMessagesTokens,
  countMessageTokens,
  countTokens,
  createContextTracker,
  getMessageText,
  type TokenizableContent,
  type TokenStats,
} from './tracker.js'
// Types
export type {
  CompactionOptions,
  CompactionResult,
  CompactionStrategy,
  Message,
  SummarizeConfig,
  SummarizeFn,
  SummaryNode,
  SummaryTree,
} from './types.js'
