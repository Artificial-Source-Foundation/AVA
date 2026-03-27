/**
 * Main Area Component
 *
 * Chat-first layout. When no session is active, shows welcome state.
 */

import { Sparkles } from 'lucide-solid'
import { type Component, Show } from 'solid-js'
import { useHq } from '../../stores/hq'
import { useSession } from '../../stores/session'
import { ChatView } from '../chat/ChatView'
import { HqShell } from '../hq'

export const MainArea: Component = () => {
  const { currentSession } = useSession()
  const { hqMode } = useHq()

  return (
    <div class="flex flex-col h-full w-full min-w-0 bg-[var(--surface)]">
      <Show
        when={hqMode()}
        fallback={
          <Show when={currentSession()} fallback={<WelcomeState />}>
            <div class="flex-1 overflow-hidden">
              <ChatView />
            </div>
          </Show>
        }
      >
        <div class="flex-1 overflow-hidden">
          <HqShell />
        </div>
      </Show>
    </div>
  )
}

/**
 * Welcome state shown when no session is active.
 */
const WelcomeState: Component = () => (
  <div class="flex-1 flex items-center justify-center h-full animate-fade-in">
    <div class="text-center animate-slide-up">
      <div
        class="
          w-16 h-16 mx-auto mb-6
          rounded-2xl
          bg-[var(--accent)]
          flex items-center justify-center
        "
        style={{ 'box-shadow': '0 0 16px rgba(139, 92, 246, 0.2)' }}
      >
        <Sparkles class="w-8 h-8 text-white" />
      </div>
      <h2 class="text-lg font-semibold text-[var(--text-primary)] mb-1">Welcome to AVA</h2>
      <p class="text-sm text-[var(--text-secondary)] mb-4">Start a new conversation to begin</p>
      <kbd class="px-2 py-1 text-xs text-[var(--text-muted)] bg-[var(--surface-raised)] border border-[var(--border-subtle)] rounded-[var(--radius-md)]">
        Ctrl+N
      </kbd>
    </div>
  </div>
)
