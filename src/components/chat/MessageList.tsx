/**
 * Message List Component
 *
 * Displays conversation messages with virtual scrolling.
 * Only renders visible messages + buffer for smooth scrolling.
 *
 * Features:
 * - Virtual scrolling for 1000+ messages
 * - Auto-scroll to new messages
 * - Dynamic height estimation
 * - Smooth scrolling during streaming
 */

import { createVirtualizer } from '@tanstack/solid-virtual'
import { Sparkles } from 'lucide-solid'
import {
  type Component,
  createEffect,
  createMemo,
  createSignal,
  For,
  onMount,
  Show,
} from 'solid-js'
import { useChat } from '../../hooks/useChat'
import { useSession } from '../../stores/session'
import { MessageBubble } from './MessageBubble'

// ============================================================================
// Constants
// ============================================================================

/** Estimated height of a message (will be measured dynamically) */
const ESTIMATED_MESSAGE_HEIGHT = 120

/** Overscan - how many items to render outside the viewport */
const OVERSCAN = 5

// ============================================================================
// Component
// ============================================================================

export const MessageList: Component = () => {
  // oxlint-disable-next-line no-unassigned-vars -- SolidJS ref pattern: assigned via ref={} in JSX
  let containerRef: HTMLDivElement | undefined
  const [shouldAutoScroll, setShouldAutoScroll] = createSignal(true)

  const {
    messages,
    isLoadingMessages,
    editingMessageId,
    retryingMessageId,
    startEditing,
    stopEditing,
  } = useSession()
  const { isStreaming, retryMessage, editAndResend, regenerateResponse } = useChat()

  // Create virtualizer
  const virtualizer = createMemo(() => {
    const messageList = messages()
    return createVirtualizer({
      get count() {
        return messageList.length
      },
      getScrollElement: () => containerRef ?? null,
      estimateSize: () => ESTIMATED_MESSAGE_HEIGHT,
      overscan: OVERSCAN,
      // Enable smooth scrolling
      scrollMargin: 0,
    })
  })

  // Get virtual items
  const virtualItems = createMemo(() => virtualizer().getVirtualItems())
  const totalSize = createMemo(() => virtualizer().getTotalSize())

  // Auto-scroll to bottom when new messages arrive
  createEffect(() => {
    const currentMessages = messages()
    const streaming = isStreaming()

    if (currentMessages.length > 0 && containerRef && shouldAutoScroll()) {
      // Use requestAnimationFrame for smooth scrolling
      requestAnimationFrame(() => {
        if (containerRef) {
          // Smooth scroll when not streaming, instant when streaming
          containerRef.scrollTo({
            top: containerRef.scrollHeight,
            behavior: streaming ? 'auto' : 'smooth',
          })
        }
      })
    }
  })

  // Track scroll position to determine if user scrolled up
  const handleScroll = () => {
    if (!containerRef) return

    const { scrollTop, scrollHeight, clientHeight } = containerRef
    const isAtBottom = scrollHeight - scrollTop - clientHeight < 100

    setShouldAutoScroll(isAtBottom)
  }

  // Scroll to bottom on mount
  onMount(() => {
    if (containerRef) {
      containerRef.scrollTop = containerRef.scrollHeight
    }
  })

  // Scroll to bottom button click
  const scrollToBottom = () => {
    if (containerRef) {
      containerRef.scrollTo({
        top: containerRef.scrollHeight,
        behavior: 'smooth',
      })
      setShouldAutoScroll(true)
    }
  }

  return (
    <div class="relative flex-1 flex flex-col">
      <div ref={containerRef} onScroll={handleScroll} class="flex-1 overflow-y-auto px-6 py-4">
        {/* Loading skeleton */}
        <Show when={isLoadingMessages()}>
          <div class="space-y-4 animate-pulse">
            <div class="h-16 bg-[var(--surface-raised)] rounded-[var(--radius-lg)] w-2/3" />
            <div class="h-24 bg-[var(--surface-raised)] rounded-[var(--radius-lg)] w-3/4 ml-auto" />
            <div class="h-16 bg-[var(--surface-raised)] rounded-[var(--radius-lg)] w-2/3" />
          </div>
        </Show>

        {/* Empty state */}
        <Show when={!isLoadingMessages() && messages().length === 0}>
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
        </Show>

        {/* Virtualized messages */}
        <Show when={!isLoadingMessages() && messages().length > 0}>
          <div
            style={{
              height: `${totalSize()}px`,
              width: '100%',
              position: 'relative',
            }}
          >
            <For each={virtualItems()}>
              {(virtualItem) => {
                const message = () => messages()[virtualItem.index]
                return (
                  <div
                    data-index={virtualItem.index}
                    ref={(el) => {
                      // eslint-disable-next-line solid/reactivity -- ref callback runs once per element mount
                      queueMicrotask(() => {
                        virtualizer().measureElement(el)
                      })
                    }}
                    style={{
                      position: 'absolute',
                      top: 0,
                      left: 0,
                      width: '100%',
                      transform: `translateY(${virtualItem.start}px)`,
                    }}
                  >
                    <Show when={message()}>
                      <div class="py-2">
                        <MessageBubble
                          message={message()!}
                          isEditing={editingMessageId() === message()?.id}
                          isRetrying={retryingMessageId() === message()?.id}
                          isStreaming={isStreaming()}
                          onStartEdit={() => startEditing(message()!.id)}
                          onCancelEdit={stopEditing}
                          onSaveEdit={(content) => editAndResend(message()!.id, content)}
                          onRetry={() => retryMessage(message()!.id)}
                          onRegenerate={() => regenerateResponse(message()!.id)}
                        />
                      </div>
                    </Show>
                  </div>
                )
              }}
            </For>
          </div>
        </Show>
      </div>

      {/* Scroll to bottom button */}
      <Show when={!shouldAutoScroll() && messages().length > 0}>
        <button
          type="button"
          onClick={scrollToBottom}
          class="
            absolute bottom-4 right-8
            p-2 rounded-full
            bg-[var(--surface-raised)] border border-[var(--border-subtle)]
            shadow-md
            text-[var(--text-secondary)]
            hover:bg-[var(--accent)] hover:text-white hover:border-[var(--accent)]
            transition-all duration-[var(--duration-fast)]
            z-10
          "
          title="Scroll to bottom"
        >
          <svg
            class="w-5 h-5"
            fill="none"
            viewBox="0 0 24 24"
            stroke="currentColor"
            aria-labelledby="scroll-icon-title"
          >
            <title id="scroll-icon-title">Scroll to bottom</title>
            <path
              stroke-linecap="round"
              stroke-linejoin="round"
              stroke-width="2"
              d="M19 14l-7 7m0 0l-7-7m7 7V3"
            />
          </svg>
        </button>
      </Show>
    </div>
  )
}
