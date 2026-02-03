/**
 * Context Module Types
 * Types for message tracking, compaction, and context management
 */

import type { ChatMessage } from '../types/llm.js'

// ============================================================================
// Message Types
// ============================================================================

/**
 * Extended message with metadata for context tracking
 * Extends ChatMessage with session and timing information
 */
export interface Message extends ChatMessage {
  /** Unique message identifier */
  id: string
  /** Session this message belongs to */
  sessionId: string
  /** Timestamp when message was created */
  createdAt: number
  /** Optional: Token count (cached for performance) */
  tokenCount?: number
}

// ============================================================================
// Compaction Types
// ============================================================================

/**
 * Strategy for compacting conversation history
 */
export interface CompactionStrategy {
  /** Strategy identifier */
  name: string
  /** Compact messages to fit within target token count */
  compact(messages: Message[], targetTokens: number): Promise<Message[]>
}

/**
 * Options for compaction
 */
export interface CompactionOptions {
  /** Target percentage of context limit to compact to (default: 50) */
  targetPercent?: number
  /** Whether to preserve the system message (default: true) */
  preserveSystem?: boolean
  /** Minimum messages to keep (default: 4) */
  minMessages?: number
}

/**
 * Result of a compaction operation
 */
export interface CompactionResult {
  /** Compacted messages */
  messages: Message[]
  /** Original message count */
  originalCount: number
  /** Final message count */
  compactedCount: number
  /** Tokens saved */
  tokensSaved: number
  /** Strategy that succeeded */
  strategyUsed: string
}

// ============================================================================
// Summarization Types
// ============================================================================

/**
 * Function type for summarizing messages via LLM
 */
export type SummarizeFn = (messages: Message[]) => Promise<string>

/**
 * Configuration for summarization strategy
 */
export interface SummarizeConfig {
  /** Number of recent messages to preserve (default: 6 = 3 turns) */
  preserveRecent?: number
  /** Custom summarization function (uses default if not provided) */
  summarizeFn?: SummarizeFn
}

// ============================================================================
// Hierarchical Types
// ============================================================================

/**
 * Node in hierarchical summary tree (Goose-inspired)
 */
export interface SummaryNode {
  /** Node identifier */
  id: string
  /** Level in tree (0 = leaf, higher = more summarized) */
  level: number
  /** Summary content */
  summary: string
  /** Token count of summary */
  tokenCount: number
  /** Child node IDs (for non-leaf nodes) */
  children?: string[]
  /** Original message IDs (for leaf nodes) */
  messageIds?: string[]
}

/**
 * Hierarchical summary tree
 */
export interface SummaryTree {
  /** Root node ID */
  rootId: string
  /** All nodes by ID */
  nodes: Map<string, SummaryNode>
  /** Maximum tree depth */
  maxLevel: number
}
