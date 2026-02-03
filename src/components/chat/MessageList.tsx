/**
 * Message List Component
 *
 * Displays conversation messages with auto-scroll.
 * Premium design with smooth animations and themed styling.
 */

import { Sparkles } from 'lucide-solid'
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
    const currentMessages = messages()
    if (currentMessages.length > 0 && containerRef) {
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
    <div
      ref={containerRef}
      class="
        flex-1 overflow-y-auto
        px-6 py-4
        space-y-4
      "
    >
      {/* Loading skeleton */}
      <Show when={isLoadingMessages()}>
        <div class="space-y-4 animate-pulse">
          <div class="h-16 bg-[var(--surface-raised)] rounded-[var(--radius-lg)] w-2/3" />
          <div class="h-24 bg-[var(--surface-raised)] rounded-[var(--radius-lg)] w-3/4 ml-auto" />
          <div class="h-16 bg-[var(--surface-raised)] rounded-[var(--radius-lg)] w-2/3" />
        </div>
      </Show>

      {/* Messages */}
      <Show when={!isLoadingMessages()}>
        <For
          each={messages()}
          fallback={
            <div class="flex flex-col items-center justify-center h-full">
              <div
                class="
                  w-16 h-16 mb-6
                  rounded-[var(--radius-xl)]
                  bg-[var(--accent-subtle)]
                  flex items-center justify-center
                "
              >
                <Sparkles class="w-8 h-8 text-[var(--accent)]" />
              </div>
              <h2 class="text-xl font-semibold text-[var(--text-primary)] font-display">
                Welcome to Estela
              </h2>
              <p class="text-sm text-[var(--text-tertiary)] mt-2 max-w-sm text-center">
                Your AI coding assistant is ready. Start a conversation to begin.
              </p>
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
