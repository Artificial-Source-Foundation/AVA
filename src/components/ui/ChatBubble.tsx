/**
 * ChatBubble Component
 *
 * Message bubbles for the chat interface.
 * Supports user messages, assistant messages, and system messages.
 */

import { type Component, type JSX, Show, splitProps } from 'solid-js'
import { Motion } from 'solid-motionone'
import { Avatar } from './Avatar'

export interface ChatBubbleProps {
  /** Message role */
  role: 'user' | 'assistant' | 'system'
  /** Message content */
  children: JSX.Element
  /** Timestamp */
  timestamp?: string
  /** Avatar image source */
  avatarSrc?: string
  /** Avatar initials fallback */
  avatarInitials?: string
  /** Show avatar */
  showAvatar?: boolean
  /** Is streaming (typing animation) */
  isStreaming?: boolean
  /** Additional CSS classes */
  class?: string
}

export const ChatBubble: Component<ChatBubbleProps> = (props) => {
  const [local, others] = splitProps(props, [
    'role',
    'children',
    'timestamp',
    'avatarSrc',
    'avatarInitials',
    'showAvatar',
    'isStreaming',
    'class',
  ])

  const showAvatar = () => local.showAvatar !== false

  const isUser = () => local.role === 'user'
  const isSystem = () => local.role === 'system'

  return (
    <Motion.div
      initial={{ opacity: 0, y: 8 }}
      animate={{ opacity: 1, y: 0 }}
      transition={{ duration: 0.25, easing: [0.34, 1.56, 0.64, 1] }}
      class={`
        flex gap-3
        ${isUser() ? 'flex-row-reverse' : 'flex-row'}
        ${isSystem() ? 'justify-center' : ''}
        ${local.class ?? ''}
      `}
      {...others}
    >
      {/* Avatar — only for system messages */}
      <Show when={showAvatar() && isSystem()}>
        <div class="flex-shrink-0">
          <Avatar src={local.avatarSrc} initials={local.avatarInitials ?? 'S'} size="sm" />
        </div>
      </Show>

      {/* Message bubble */}
      <div
        class={`
          ${isUser() ? 'max-w-[70%]' : isSystem() ? 'max-w-full' : 'w-full'}
        `}
      >
        <div
          class={`
            text-sm
            leading-relaxed
            ${
              isUser()
                ? `
              px-4 py-3
              bg-[var(--chat-user-bg)]
              text-[var(--chat-user-text)]
              rounded-[16px] rounded-br-[4px]
            `
                : isSystem()
                  ? `
              px-4 py-2.5
              bg-[var(--surface-sunken)]
              text-[var(--text-tertiary)]
              text-xs
              italic
              rounded-[var(--radius-lg)]
            `
                  : `
              text-[var(--chat-assistant-text)]
            `
            }
          `}
        >
          {local.children}

          {/* Streaming cursor */}
          <Show when={local.isStreaming}>
            <span class="inline-block w-2 h-4 ml-1 bg-current animate-pulse-subtle rounded-sm" />
          </Show>
        </div>

        {/* Timestamp */}
        <Show when={local.timestamp}>
          <div
            class={`
              mt-1 text-2xs text-[var(--text-muted)]
              ${isUser() ? 'text-right' : 'text-left'}
            `}
          >
            {local.timestamp}
          </div>
        </Show>
      </div>
    </Motion.div>
  )
}

/**
 * TypingIndicator - Shows when assistant is typing
 */
export const TypingIndicator: Component<{ class?: string }> = (props) => (
  <div class={`flex gap-3 ${props.class ?? ''}`}>
    <div class="py-2">
      <div class="flex gap-1">
        <span class="w-2 h-2 bg-[var(--text-tertiary)] rounded-full animate-bounce [animation-delay:-0.3s]" />
        <span class="w-2 h-2 bg-[var(--text-tertiary)] rounded-full animate-bounce [animation-delay:-0.15s]" />
        <span class="w-2 h-2 bg-[var(--text-tertiary)] rounded-full animate-bounce" />
      </div>
    </div>
  </div>
)
