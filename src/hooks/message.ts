/**
 * Delta9 Message Hooks
 *
 * Intercepts messages before and after processing for:
 * - Context injection (insights, patterns, warnings)
 * - Message transformation
 * - Response enhancement
 * - Logging and monitoring
 */

import type { MissionState } from '../mission/state.js'
import { generateCoordinatorInsights, formatInsightsForPrompt } from '../learning/insights.js'
import { getEventStore } from '../events/store.js'

// =============================================================================
// Types
// =============================================================================

export interface MessageHooksInput {
  /** Mission state instance */
  state: MissionState
  /** Project root directory */
  cwd: string
  /** Logger function */
  log: (level: string, message: string, data?: Record<string, unknown>) => void
}

/** Message content part */
export interface MessagePart {
  type: 'text' | 'tool_use' | 'tool_result' | 'image' | 'reasoning'
  text?: string
  id?: string
  name?: string
  input?: Record<string, unknown>
}

/** Message before hook input */
export interface MessageBeforeInput {
  /** Session ID */
  sessionID: string
  /** Message role */
  role: 'user' | 'assistant' | 'system'
  /** Message parts */
  parts: MessagePart[]
}

/** Message before hook output (mutable) */
export interface MessageBeforeOutput {
  /** Modified message parts */
  parts: MessagePart[]
  /** Extra content to prepend to system prompt */
  systemPrepend?: string
  /** Extra content to append to system prompt */
  systemAppend?: string
}

/** Message after hook input */
export interface MessageAfterInput {
  /** Session ID */
  sessionID: string
  /** Message role */
  role: 'user' | 'assistant' | 'system'
  /** Message parts */
  parts: MessagePart[]
  /** Processing duration (ms) */
  duration?: number
}

/** Message after hook output */
export interface MessageAfterOutput {
  /** Whether to continue processing */
  continue: boolean
  /** Optional response to inject */
  inject?: MessagePart[]
}

export interface MessageHooks {
  /** Hook before message is processed */
  'message.before': (input: MessageBeforeInput, output: MessageBeforeOutput) => Promise<void>
  /** Hook after message is processed */
  'message.after': (input: MessageAfterInput, output: MessageAfterOutput) => Promise<void>
}

// =============================================================================
// State Tracking
// =============================================================================

/** Track message counts per session for analytics */
const messageCountMap = new Map<string, { user: number; assistant: number; total: number }>()

/**
 * Get message stats for a session
 */
export function getMessageStats(sessionId: string): {
  user: number
  assistant: number
  total: number
} {
  return messageCountMap.get(sessionId) ?? { user: 0, assistant: 0, total: 0 }
}

/**
 * Clear message stats (for testing)
 */
export function clearMessageStats(): void {
  messageCountMap.clear()
}

// =============================================================================
// Hook Factory
// =============================================================================

/**
 * Create message hooks with closure over state
 */
export function createMessageHooks(input: MessageHooksInput): MessageHooks {
  const { state, log } = input

  return {
    /**
     * Before Message Processing
     *
     * - Inject learning insights for assistant messages
     * - Inject mission context for user messages
     * - Log message activity
     */
    'message.before': async (messageInput, output) => {
      const { sessionID, role, parts } = messageInput

      // Update message counts
      const stats = messageCountMap.get(sessionID) ?? { user: 0, assistant: 0, total: 0 }
      stats.total++
      if (role === 'user') stats.user++
      else if (role === 'assistant') stats.assistant++
      messageCountMap.set(sessionID, stats)

      // Only inject insights for assistant turns (when we're about to generate)
      if (role === 'assistant') {
        const mission = state.getMission()
        if (!mission) return

        // Find relevant files from current context
        const filesInContext: string[] = []
        for (const part of parts) {
          if (part.type === 'text' && part.text) {
            // Extract file paths mentioned in text
            const fileMatches = part.text.match(
              /[a-zA-Z0-9_\-./]+\.(ts|tsx|js|jsx|json|md|css|html)/g
            )
            if (fileMatches) {
              filesInContext.push(...fileMatches)
            }
          }
        }

        // Generate insights from learning system
        try {
          const insights = generateCoordinatorInsights({
            files: filesInContext.length > 0 ? filesInContext : undefined,
          })

          if (insights.length > 0) {
            const insightsText = formatInsightsForPrompt(insights)

            // Inject insights as system append
            if (insightsText) {
              output.systemAppend = `\n\n<learning-insights>\n${insightsText}\n</learning-insights>`

              log('debug', 'Injected learning insights', {
                sessionId: sessionID,
                insightCount: insights.length,
              })
            }
          }
        } catch (error) {
          log('warn', 'Failed to generate insights', { error })
        }

        // Inject current mission status
        if (mission.status === 'in_progress') {
          const progress = state.getProgress()
          const inProgressTask = findInProgressTask(state)

          if (inProgressTask) {
            const missionContext = `
<mission-context>
Mission: ${mission.description}
Progress: ${progress}%
Current Task: ${inProgressTask.description}
</mission-context>`

            output.systemAppend = (output.systemAppend ?? '') + missionContext
          }
        }
      }
    },

    /**
     * After Message Processing
     *
     * - Log message completion
     * - Record events for analytics
     * - Check for mission-relevant content
     */
    'message.after': async (messageInput, output) => {
      const { sessionID, role, parts, duration } = messageInput

      // Log message completion
      log('debug', `Message processed: ${role}`, {
        sessionId: sessionID,
        partCount: parts.length,
        duration,
      })

      // Record event for analytics
      try {
        const eventStore = getEventStore()
        eventStore.append('agent.completed', {
          agent: 'message_processor',
          taskId: sessionID,
          success: true,
          duration: duration ?? 0,
        })
      } catch {
        // Event store may not be initialized
      }

      // Note: Completion detection based on language patterns was removed
      // as it was unreliable. Task completion should be explicitly tracked
      // through the mission state system instead.

      // Default: continue processing
      output.continue = true
    },
  }
}

// =============================================================================
// Helpers
// =============================================================================

/**
 * Find the currently in-progress task
 */
function findInProgressTask(
  state: MissionState
): { id: string; description: string; filesChanged?: string[] } | null {
  const mission = state.getMission()
  if (!mission) return null

  for (const objective of mission.objectives) {
    for (const task of objective.tasks) {
      if (task.status === 'in_progress') {
        return {
          id: task.id,
          description: task.description,
          filesChanged: task.filesChanged,
        }
      }
    }
  }
  return null
}
