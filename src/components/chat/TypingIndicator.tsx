/**
 * Typing Indicator Component
 *
 * Animated dots shown while streaming a response.
 * Renders inline within the message bubble — no extra container.
 */

import type { Component } from 'solid-js'

interface TypingIndicatorProps {
  label?: string
}

export const TypingIndicator: Component<TypingIndicatorProps> = (props) => {
  return (
    <div class="flex items-center gap-2.5 py-0.5">
      <div class="flex items-center gap-[5px]">
        <span class="typing-dot" style={{ 'animation-delay': '0ms' }} />
        <span class="typing-dot" style={{ 'animation-delay': '160ms' }} />
        <span class="typing-dot" style={{ 'animation-delay': '320ms' }} />
      </div>
      <span class="text-xs text-[var(--text-secondary)] font-[var(--font-ui-mono)] tracking-wide">
        {props.label ?? 'Thinking'}
      </span>
    </div>
  )
}
