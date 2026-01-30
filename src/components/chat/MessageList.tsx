/**
 * MessageList Component
 * Displays conversation messages with auto-scroll
 */

import { type Component, createEffect, For, onMount, Show } from 'solid-js'
import { useChat } from '../../hooks/useChat'
import { useSession } from '../../stores/session'
import { MessageBubble } from './MessageBubble'

export const MessageList: Component = () => {
  let containerRef: HTMLDivElement | undefined
  const {
    messages,
    isLoadingMessages,
    editingMessageId,
    retryingMessageId,
    startEditing,
    stopEditing,
  } = useSession()
  const { isStreaming, retryMessage, editAndResend, regenerateResponse } = useChat()

  // Auto-scroll to bottom when messages change
  createEffect(() => {
    // Track messages dependency
    const currentMessages = messages()
    if (currentMessages.length > 0 && containerRef) {
      // Use requestAnimationFrame for smooth scroll
      requestAnimationFrame(() => {
        containerRef?.scrollTo({
          top: containerRef.scrollHeight,
          behavior: 'smooth',
        })
      })
    }
  })

  // Scroll to bottom on mount
  onMount(() => {
    if (containerRef) {
      containerRef.scrollTop = containerRef.scrollHeight
    }
  })

  return (
    <div ref={containerRef} class="flex-1 overflow-y-auto p-4 space-y-4">
      {/* Loading skeleton */}
      <Show when={isLoadingMessages()}>
        <div class="space-y-4 animate-pulse">
          <div class="h-16 bg-gray-700 rounded-lg w-2/3" />
          <div class="h-24 bg-gray-700 rounded-lg w-3/4 ml-auto" />
          <div class="h-16 bg-gray-700 rounded-lg w-2/3" />
        </div>
      </Show>

      {/* Messages */}
      <Show when={!isLoadingMessages()}>
        <For
          each={messages()}
          fallback={
            <div class="flex items-center justify-center h-full text-gray-500">
              <div class="text-center">
                <p class="text-lg">Welcome to Estela</p>
                <p class="text-sm mt-2">Start a conversation to begin</p>
              </div>
            </div>
          }
        >
          {(message) => (
            <MessageBubble
              message={message}
              isEditing={editingMessageId() === message.id}
              isRetrying={retryingMessageId() === message.id}
              isStreaming={isStreaming()}
              onStartEdit={() => startEditing(message.id)}
              onCancelEdit={stopEditing}
              onSaveEdit={(content) => editAndResend(message.id, content)}
              onRetry={() => retryMessage(message.id)}
              onRegenerate={() => regenerateResponse(message.id)}
            />
          )}
        </For>
      </Show>
    </div>
  )
}
