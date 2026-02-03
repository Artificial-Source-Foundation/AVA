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
  extractSummary,
  getSummarizationPrompt,
  type HierarchicalConfig,
  hierarchical,
  type SlidingWindowOptions,
  selectLevel,
  slidingWindow,
  summarize,
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
