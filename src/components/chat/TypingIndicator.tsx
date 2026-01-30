/**
 * TypingIndicator Component
 * Animated dots shown while streaming a response
 */

import type { Component } from 'solid-js'

export const TypingIndicator: Component = () => {
  return (
    <div class="flex items-center gap-1 py-1">
      <span
        class="w-2 h-2 bg-gray-400 rounded-full animate-bounce"
        style={{ 'animation-delay': '0ms' }}
      />
      <span
        class="w-2 h-2 bg-gray-400 rounded-full animate-bounce"
        style={{ 'animation-delay': '150ms' }}
      />
      <span
        class="w-2 h-2 bg-gray-400 rounded-full animate-bounce"
        style={{ 'animation-delay': '300ms' }}
      />
    </div>
  )
}
