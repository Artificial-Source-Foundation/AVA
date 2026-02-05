/**
 * Chat View Component
 *
 * Main chat container with session loading.
 * Premium layout with seamless message flow.
 * Includes tool approval dialog for agent mode.
 */

import { type Component, createEffect, on } from 'solid-js'
import { useAgent } from '../../hooks/useAgent'
import { useSession } from '../../stores/session'
import { ToolApprovalDialog } from '../dialogs/ToolApprovalDialog'
import { MessageInput } from './MessageInput'
import { MessageList } from './MessageList'

export const ChatView: Component = () => {
  const { currentSession, loadSessionMessages, clearSession } = useSession()
  const agent = useAgent()

  // Load messages when session changes
  createEffect(
    on(
      () => currentSession()?.id,
      (sessionId, prevSessionId) => {
        if (sessionId && sessionId !== prevSessionId) {
          loadSessionMessages(sessionId)
        } else if (!sessionId && prevSessionId) {
          clearSession()
        }
      }
    )
  )

  // Handle tool approval resolution
  const handleApprovalResolve = (approved: boolean, _alwaysAllow?: boolean) => {
    // TODO: If alwaysAllow is true, persist to session settings
    agent.resolveApproval(approved)
  }

  return (
    <div class="flex flex-col h-full bg-[var(--surface)]">
      {/* Messages area */}
      <MessageList />

      {/* Input area */}
      <MessageInput />

      {/* Tool Approval Dialog */}
      <ToolApprovalDialog request={agent.pendingApproval()} onResolve={handleApprovalResolve} />
    </div>
  )
}
