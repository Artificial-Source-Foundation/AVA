/**
 * Chat View Component
 *
 * Main chat container with session loading.
 * Premium layout with seamless message flow.
 */

import { type Component, createEffect, on } from 'solid-js'
import { useSession } from '../../stores/session'
import { MessageInput } from './MessageInput'
import { MessageList } from './MessageList'

export const ChatView: Component = () => {
  const { currentSession, loadSessionMessages, clearSession } = useSession()

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

  return (
    <div
      class="
        flex flex-col h-full
        bg-[var(--background)]
        transition-colors duration-[var(--duration-normal)]
      "
    >
      {/* Messages area */}
      <MessageList />

      {/* Input area */}
      <MessageInput />
    </div>
  )
}
