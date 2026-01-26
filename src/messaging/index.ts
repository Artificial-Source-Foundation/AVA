/**
 * Delta9 Agent Messaging System
 *
 * Inbox/outbox messaging for agent-to-agent coordination.
 *
 * @example
 * ```typescript
 * import { getMessageStore, type Message } from './messaging'
 *
 * const store = getMessageStore()
 *
 * // Send a message
 * const result = store.send('commander', {
 *   to: 'operator',
 *   type: 'request',
 *   subject: 'Execute task',
 *   body: 'Please implement the authentication system',
 *   taskId: 'task-123',
 * })
 *
 * // Check inbox
 * const inbox = store.getInbox({ agentId: 'operator', unreadOnly: true })
 *
 * // Reply to a message
 * store.reply('operator', result.messageId!, 'Task completed', { type: 'response' })
 * ```
 */

// Types
export {
  type Message,
  type MessageRecipient,
  type MessageType,
  type MessagePriority,
  type InboxQuery,
  type SendMessageOptions,
  type MessageStoreConfig,
  type MessageEvent,
  type MessageEventType,
  type MessageEventListener,
  type SendResult,
  type ReadResult,
  type InboxResult,
  type AgentGroup,
  MessageSchema,
  MessageRecipientSchema,
  MessageTypeSchema,
  MessagePrioritySchema,
  InboxQuerySchema,
  SendMessageOptionsSchema,
  DEFAULT_MESSAGE_CONFIG,
  AGENT_GROUPS,
} from './types.js'

// Store
export { MessageStore, getMessageStore, resetMessageStore } from './store.js'

// Session State
export {
  SessionStateManager,
  getSessionStateManager,
  resetSessionStateManager,
  type SessionState,
  type SessionInfo,
  type ResumeCallback,
  type ResumeReason,
  type SessionStateConfig,
} from './session-state.js'
