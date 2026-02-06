/**
 * Verified Summarization Strategy
 *
 * Enhanced summarization with:
 * 1. State snapshot format (instead of generic summaries)
 * 2. Safe split point detection
 * 3. Self-correction verification turn
 *
 * Inspired by Gemini CLI's 3-phase compression:
 * - Phase 1: Tool truncation (separate strategy)
 * - Phase 2: Split & summarize with state snapshots
 * - Phase 3: Verification probe for self-correction
 *
 * The state snapshot format persists across compressions,
 * allowing incremental updates rather than starting fresh.
 */

import { countTokens } from '../tracker.js'
import type { CompactionStrategy, Message, SummarizeFn } from '../types.js'
import { findSafeSplitPoint } from './split-point.js'

// ============================================================================
// Types
// ============================================================================

export interface VerifiedSummarizeConfig {
  /** Fraction of messages to preserve (default: 0.3 = keep 30%) */
  preserveFraction?: number
  /** LLM function for generating summaries */
  summarizeFn: SummarizeFn
  /** LLM function for verification (optional, uses summarizeFn if not set) */
  verifyFn?: SummarizeFn
  /** Whether to run verification probe (default: true) */
  enableVerification?: boolean
  /** Session ID for created messages */
  sessionId?: string
}

// ============================================================================
// Prompts
// ============================================================================

const STATE_SNAPSHOT_PROMPT = `You are creating a state snapshot of the conversation so far. Generate a <state_snapshot> that captures:

1. **Current Goal**: What the user is trying to accomplish
2. **Progress**: What has been done so far (specific files modified, commands run, decisions made)
3. **Key Context**: File paths, function names, variable names, error messages - be SPECIFIC
4. **Pending Work**: What still needs to be done
5. **Constraints**: Any limitations or requirements established

Format your response as:
<state_snapshot>
[Your detailed snapshot here with specific technical details]
</state_snapshot>

CRITICAL: Include specific file paths, line numbers, function names, and error messages. Generic summaries are NOT useful.`

const VERIFICATION_PROMPT = `Critically evaluate the <state_snapshot> you just generated. Did you omit any specific:
- File paths or directories mentioned in the history?
- Function/variable/class names that were discussed?
- Error messages or stack traces that were encountered?
- Tool outputs or command results?
- User constraints or requirements?

If anything is missing or could be more precise, generate a FINAL, improved <state_snapshot>.
Otherwise, repeat the exact same <state_snapshot> again.`

// ============================================================================
// Strategy
// ============================================================================

/**
 * Create a verified summarization strategy.
 *
 * This is the most accurate compression strategy, suitable for
 * long coding sessions where preserving technical details matters.
 */
export function createVerifiedSummarize(config: VerifiedSummarizeConfig): CompactionStrategy {
  const {
    preserveFraction = 0.3,
    summarizeFn,
    verifyFn,
    enableVerification = true,
    sessionId = 'compaction',
  } = config

  return {
    name: 'verified-summarize',

    async compact(messages: Message[], _targetTokens: number): Promise<Message[]> {
      if (messages.length === 0) return []

      // Separate system message
      const systemMessage = messages.find((m) => m.role === 'system')
      const conversationMessages = messages.filter((m) => m.role !== 'system')

      if (conversationMessages.length <= 4) {
        return messages // Too few to compress
      }

      // Find safe split point
      const splitIndex = findSafeSplitPoint(conversationMessages, preserveFraction)

      if (splitIndex === 0) {
        return messages // Nothing safe to compress
      }

      const olderMessages = conversationMessages.slice(0, splitIndex)
      const recentMessages = conversationMessages.slice(splitIndex)

      // Check for existing state snapshot in messages
      const existingSnapshot = findExistingSnapshot(messages)

      // Generate state snapshot via LLM
      const snapshotMessages = existingSnapshot
        ? [createSnapshotContextMessage(existingSnapshot, sessionId), ...olderMessages]
        : olderMessages

      let snapshot = await summarizeFn(snapshotMessages)

      // Verify snapshot quality
      if (enableVerification && (verifyFn ?? summarizeFn)) {
        const verifier = verifyFn ?? summarizeFn
        const verificationInput: Message[] = [
          createMessage(sessionId, 'assistant', snapshot),
          createMessage(sessionId, 'user', VERIFICATION_PROMPT),
        ]
        const verifiedSnapshot = await verifier(verificationInput)

        // Use verified version if it's not empty and has reasonable size
        if (verifiedSnapshot && verifiedSnapshot.length > 0) {
          const originalTokens = countTokens(snapshot)
          const verifiedTokens = countTokens(verifiedSnapshot)

          // Accept verified if it's not drastically inflated (< 2x)
          if (verifiedTokens < originalTokens * 2) {
            snapshot = verifiedSnapshot
          }
        }
      }

      // Validate: snapshot should not be empty
      if (!snapshot || snapshot.trim().length === 0) {
        return messages // Summarization failed
      }

      // Validate: snapshot should be smaller than original
      const originalTokens = countTokens(olderMessages.map((m) => m.content).join('\n'))
      const snapshotTokens = countTokens(snapshot)

      if (snapshotTokens >= originalTokens) {
        return messages // Inflation detected, skip
      }

      // Build result: system + snapshot + recent messages
      const snapshotMessage = createSnapshotMessage(snapshot, sessionId)
      const result: Message[] = []

      if (systemMessage) {
        result.push(systemMessage)
      }
      result.push(snapshotMessage)
      result.push(...recentMessages)

      return result
    },
  }
}

// ============================================================================
// State Snapshot Helpers
// ============================================================================

/** Tag used to identify state snapshots in message content */
export const STATE_SNAPSHOT_TAG = '<state_snapshot>'
export const STATE_SNAPSHOT_CLOSE_TAG = '</state_snapshot>'

/**
 * Extract a state snapshot from message content.
 */
export function extractStateSnapshot(content: string): string | null {
  const startTag = STATE_SNAPSHOT_TAG
  const endTag = STATE_SNAPSHOT_CLOSE_TAG

  const startIdx = content.indexOf(startTag)
  const endIdx = content.indexOf(endTag)

  if (startIdx === -1 || endIdx === -1 || endIdx <= startIdx) {
    return null
  }

  return content.slice(startIdx + startTag.length, endIdx).trim()
}

/**
 * Find an existing state snapshot in the conversation history.
 * Searches system messages for prior snapshots.
 */
function findExistingSnapshot(messages: Message[]): string | null {
  for (const msg of messages) {
    if (msg.role === 'system' && msg.content.includes(STATE_SNAPSHOT_TAG)) {
      return extractStateSnapshot(msg.content)
    }
  }
  return null
}

/**
 * Create a snapshot system message for the compacted history.
 */
function createSnapshotMessage(snapshot: string, sessionId: string): Message {
  // Wrap in tags if not already wrapped
  const content = snapshot.includes(STATE_SNAPSHOT_TAG)
    ? snapshot
    : `${STATE_SNAPSHOT_TAG}\n${snapshot}\n${STATE_SNAPSHOT_CLOSE_TAG}`

  return {
    id: `snapshot-${Date.now()}`,
    sessionId,
    role: 'system',
    content: `[Conversation State Snapshot]\n${content}`,
    createdAt: Date.now(),
    tokenCount: countTokens(content),
  }
}

/**
 * Create a context message from an existing snapshot (for re-summarization).
 */
function createSnapshotContextMessage(snapshot: string, sessionId: string): Message {
  return {
    id: `prev-snapshot-${Date.now()}`,
    sessionId,
    role: 'system',
    content: `[Previous state - MUST integrate into new snapshot, do not lose constraints]\n${STATE_SNAPSHOT_TAG}\n${snapshot}\n${STATE_SNAPSHOT_CLOSE_TAG}`,
    createdAt: Date.now(),
  }
}

/**
 * Create a simple message (helper).
 */
function createMessage(
  sessionId: string,
  role: 'user' | 'assistant' | 'system',
  content: string
): Message {
  return {
    id: `gen-${Date.now()}-${Math.random().toString(36).slice(2, 6)}`,
    sessionId,
    role,
    content,
    createdAt: Date.now(),
  }
}

/**
 * Get the summarization system prompt for state snapshots.
 * Can be used by callers when constructing their summarizeFn.
 */
export function getStateSnapshotPrompt(): string {
  return STATE_SNAPSHOT_PROMPT
}

/**
 * Get the verification prompt.
 * Can be used by callers for custom verification flows.
 */
export function getVerificationPrompt(): string {
  return VERIFICATION_PROMPT
}
