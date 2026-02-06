/**
 * Context Compaction Strategies
 */

export {
  buildSummaryTree,
  clearHierarchicalCache,
  createHierarchical,
  type HierarchicalConfig,
  hierarchical,
  selectLevel,
} from './hierarchical.js'
export { createSlidingWindow, type SlidingWindowOptions, slidingWindow } from './sliding-window.js'
// Split point detection
export {
  DEFAULT_PRESERVE_FRACTION,
  findAllSplitPoints,
  findSafeSplitPoint,
  findSizeSplitPoint,
  getContentSizeUpTo,
  MIN_PRESERVE_MESSAGES,
} from './split-point.js'
export {
  createSummarize,
  extractSummary,
  getSummarizationPrompt,
  summarize,
} from './summarize.js'
// Tool output truncation
export {
  createToolTruncation,
  type ToolTruncationConfig,
  toolTruncation,
  truncateContent,
} from './tool-truncation.js'
// Verified summarization with state snapshots
export {
  createVerifiedSummarize,
  extractStateSnapshot,
  getStateSnapshotPrompt,
  getVerificationPrompt,
  STATE_SNAPSHOT_CLOSE_TAG,
  STATE_SNAPSHOT_TAG,
  type VerifiedSummarizeConfig,
} from './verified-summarize.js'
