/**
 * Delta9 Session State Tools
 *
 * Tools for managing session states and resumption.
 */

import { tool, type ToolDefinition } from '@opencode-ai/plugin'
import { getSessionStateManager, getMessageStore, type SessionState } from '../messaging/index.js'

// Use the tool's built-in schema (Zod 4 compatible)
const s = tool.schema

// =============================================================================
// Tool Factory
// =============================================================================

export function createSessionStateTools(): Record<string, ToolDefinition> {
  const stateManager = getSessionStateManager()
  const messageStore = getMessageStore()

  // Wire up the message listener for auto-resume
  const messageListener = stateManager.createMessageListener()
  messageStore.on(messageListener)

  /**
   * Register a session
   */
  const register_session = tool({
    description: `Register a session for state tracking and auto-resume.

**Purpose:** Enable session state management for an agent.

**Features:**
- Tracks active/idle/completed state
- Auto-resumes when messages arrive for idle sessions
- Tracks pending message count

**Example:**
- register_session(agent_id="my_worker")`,

    args: {
      agent_id: s.string().describe('Agent ID to register'),
    },

    async execute(args, ctx) {
      const sessionId = ctx.sessionID || `session_${Date.now()}`

      const session = stateManager.registerSession(sessionId, args.agent_id)

      return JSON.stringify({
        success: true,
        session_id: session.sessionId,
        agent_id: session.agentId,
        state: session.state,
        message: `Session registered for agent "${args.agent_id}"`,
      })
    },
  })

  /**
   * Set session state
   */
  const set_session_state = tool({
    description: `Set the current session state.

**Purpose:** Manually control session state for pause/resume patterns.

**States:**
- active: Session is actively working
- idle: Session is waiting for input
- completed: Session is done

**Example:**
- set_session_state(state="idle") - Mark session as waiting`,

    args: {
      state: s.enum(['active', 'idle', 'completed']).describe('New session state'),
    },

    async execute(args, ctx) {
      const sessionId = ctx.sessionID
      if (!sessionId) {
        return JSON.stringify({
          success: false,
          error: 'No session ID available',
        })
      }

      stateManager.setState(sessionId, args.state as SessionState)

      const session = stateManager.getSession(sessionId)

      return JSON.stringify({
        success: true,
        session_id: sessionId,
        state: args.state,
        pending_messages: session?.pendingMessages || 0,
        message: `Session state set to "${args.state}"`,
      })
    },
  })

  /**
   * Get session state
   */
  const get_session_state = tool({
    description: `Get the current session state and info.

**Purpose:** Check session status, pending messages, and activity.`,

    args: {
      session_id: s.string().optional().describe('Session ID (defaults to current)'),
    },

    async execute(args, ctx) {
      const sessionId = args.session_id || ctx.sessionID

      if (!sessionId) {
        return JSON.stringify({
          success: false,
          error: 'No session ID available',
        })
      }

      const session = stateManager.getSession(sessionId)

      if (!session) {
        return JSON.stringify({
          success: false,
          error: `Session not found: ${sessionId}`,
        })
      }

      return JSON.stringify({
        success: true,
        session: {
          session_id: session.sessionId,
          agent_id: session.agentId,
          state: session.state,
          pending_messages: session.pendingMessages,
          created_at: session.createdAt,
          last_state_change: session.lastStateChange,
          last_activity: session.lastActivity,
        },
      })
    },
  })

  /**
   * List sessions
   */
  const list_sessions = tool({
    description: `List all registered sessions.

**Purpose:** See which sessions are active, idle, or have pending messages.`,

    args: {
      state: s.enum(['active', 'idle', 'completed']).optional().describe('Filter by state'),
    },

    async execute(args, _ctx) {
      const sessions = stateManager.listSessions(
        args.state ? { state: args.state as SessionState } : undefined
      )

      const idleWithPending = stateManager.getIdleSessionsWithPendingMessages()

      return JSON.stringify({
        success: true,
        total: sessions.length,
        idle_with_pending: idleWithPending.length,
        sessions: sessions.map((s) => ({
          session_id: s.sessionId,
          agent_id: s.agentId,
          state: s.state,
          pending_messages: s.pendingMessages,
        })),
      })
    },
  })

  /**
   * Trigger resume
   */
  const trigger_resume = tool({
    description: `Manually trigger resume for an idle session.

**Purpose:** Wake up an idle session without waiting for a message.`,

    args: {
      session_id: s.string().describe('Session ID to resume'),
      reason: s.string().optional().describe('Reason for resuming'),
    },

    async execute(args, _ctx) {
      const success = await stateManager.triggerResume(args.session_id, {
        type: 'manual',
      })

      if (!success) {
        const session = stateManager.getSession(args.session_id)
        return JSON.stringify({
          success: false,
          error: session
            ? `Session is ${session.state}, not idle`
            : `Session not found: ${args.session_id}`,
        })
      }

      return JSON.stringify({
        success: true,
        session_id: args.session_id,
        message: 'Session resumed',
      })
    },
  })

  /**
   * Check for pending resumes
   */
  const check_pending_resumes = tool({
    description: `Check for idle sessions with pending messages that need attention.

**Purpose:** Find sessions waiting to be resumed due to unread messages.`,

    args: {},

    async execute(_args, _ctx) {
      const pending = stateManager.getIdleSessionsWithPendingMessages()

      return JSON.stringify({
        success: true,
        count: pending.length,
        sessions: pending.map((s) => ({
          session_id: s.sessionId,
          agent_id: s.agentId,
          pending_messages: s.pendingMessages,
          idle_since: s.lastStateChange,
        })),
        message:
          pending.length > 0
            ? `${pending.length} session(s) have pending messages`
            : 'No sessions have pending messages',
      })
    },
  })

  return {
    register_session,
    set_session_state,
    get_session_state,
    list_sessions,
    trigger_resume,
    check_pending_resumes,
  }
}

export const SESSION_STATE_TOOL_NAMES = [
  'register_session',
  'set_session_state',
  'get_session_state',
  'list_sessions',
  'trigger_resume',
  'check_pending_resumes',
] as const
