/**
 * Turn Manager — DEPRECATED
 *
 * Agent execution now happens via the Rust backend (useRustAgent).
 * This module is retained for type compatibility and message persistence helpers.
 * The run/cancel/steer lifecycle is handled by useAgent -> useRustAgent.
 */

import { saveMessage } from '../../services/database'
import type { Message } from '../../types'
import type { QueuedMessage } from '../chat/types'
import type { ConfigDeps } from './config-builder'
import type { AgentSignals, SessionBridge } from './types'

// ============================================================================
// Message Helpers (still used for DB persistence)
// ============================================================================

/** Create and persist a user message, adding it to the session store. */
export async function createUserMessage(
  sessionId: string,
  content: string,
  session: SessionBridge,
  images?: QueuedMessage['images']
): Promise<Message> {
  const msg = await saveMessage({
    sessionId,
    role: 'user',
    content,
    metadata: images?.length ? { images } : undefined,
  })
  session.addMessage(msg)
  return msg
}

/** Create and persist an empty assistant message placeholder. */
export async function createAssistantMessage(
  sessionId: string,
  session: SessionBridge
): Promise<Message> {
  const msg = await saveMessage({ sessionId, role: 'assistant', content: '' })
  session.addMessage(msg)
  return msg
}

// ============================================================================
// Turn Manager Types (kept for backward compat)
// ============================================================================

export interface TurnManagerDeps {
  signals: AgentSignals
  refs: { abortRef: { current: AbortController | null }; executorRef: { current: unknown | null } }
  session: SessionBridge
  handleAgentEvent: (event: unknown) => void
  configDeps: ConfigDeps
  teamStore: { clearTeam: () => void }
}

export interface TurnManager {
  run: (goal: string, config?: Record<string, unknown>) => Promise<unknown>
  cancel: () => void
  steer: (
    content: string,
    model?: string,
    images?: Array<{ data: string; mimeType: string; name?: string }>
  ) => void
  processQueue: () => Promise<void>
  clearQueue: () => void
  removeFromQueue: (index: number) => void
  retryMessage: (assistantMessageId: string) => Promise<void>
  editAndResend: (messageId: string, newContent: string) => Promise<void>
  regenerateResponse: (assistantMessageId: string) => Promise<void>
  undoLastEdit: () => Promise<{ success: boolean; message: string }>
}

/**
 * @deprecated Turn manager is no longer used. Agent execution flows through useRustAgent.
 */
export function createTurnManager(_deps: TurnManagerDeps): TurnManager {
  return {
    run: async () => null,
    cancel: () => {},
    steer: () => {},
    processQueue: async () => {},
    clearQueue: () => {},
    removeFromQueue: () => {},
    retryMessage: async () => {},
    editAndResend: async () => {},
    regenerateResponse: async () => {},
    undoLastEdit: async () => ({ success: false, message: 'Not implemented — use Rust backend' }),
  }
}
