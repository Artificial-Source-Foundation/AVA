/**
 * StatusBar Component
 * Shows status, token usage, and version info
 */

import { type Component, Show } from 'solid-js'
import { useSession } from '../../stores/session'

export const StatusBar: Component = () => {
  const { sessionTokenStats, currentSession } = useSession()

  return (
    <div class="flex items-center justify-between h-6 bg-gray-800 border-t border-gray-700 px-4 text-xs text-gray-500">
      {/* Left side - Agent status */}
      <div class="flex items-center space-x-4">
        <span class="flex items-center">
          <span class="w-2 h-2 rounded-full bg-green-500 mr-2"></span>
          Ready
        </span>
      </div>

      {/* Right side - Token count and info */}
      <div class="flex items-center space-x-4">
        {/* Session token counter */}
        <Show when={currentSession() && sessionTokenStats().total > 0}>
          <span title="Session token usage">
            {sessionTokenStats().total.toLocaleString()} tokens
          </span>
        </Show>
        <span>Estela v0.1.0</span>
      </div>
    </div>
  )
}
