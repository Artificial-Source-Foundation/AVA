/**
 * Session List Component
 *
 * Displays all chat sessions with create new button.
 * Premium design with proper theming and animations.
 */

import { MessageSquare, Plus, Search } from 'lucide-solid'
import { type Component, For, onMount, Show } from 'solid-js'
import { useSession } from '../../stores/session'
import { SessionListItem } from './SessionListItem'

export const SessionList: Component = () => {
  const {
    sessions,
    isLoadingSessions,
    currentSession,
    loadAllSessions,
    createNewSession,
    switchSession,
    renameSession,
    archiveSession,
  } = useSession()

  // Load sessions on mount
  onMount(() => {
    loadAllSessions()
  })

  const handleCreateSession = async () => {
    await createNewSession()
  }

  return (
    <div class="flex flex-col h-full">
      {/* New Chat button */}
      <div class="p-3 pb-0">
        <button
          type="button"
          onClick={handleCreateSession}
          class="
            w-full flex items-center justify-center gap-2
            px-4 py-2.5
            bg-[var(--accent)] hover:bg-[var(--accent-hover)]
            text-white font-medium text-sm
            rounded-[var(--radius-lg)]
            transition-all duration-[var(--duration-fast)]
            active:scale-[0.98]
            shadow-sm
          "
        >
          <Plus class="w-4 h-4" />
          New Chat
        </button>
      </div>

      {/* Search (placeholder for future) */}
      <div class="p-3 pb-2">
        <div
          class="
            flex items-center gap-2
            px-3 py-2
            bg-[var(--surface-sunken)]
            border border-[var(--border-subtle)]
            rounded-[var(--radius-lg)]
            text-[var(--text-tertiary)]
            text-sm
          "
        >
          <Search class="w-4 h-4" />
          <span>Search chats...</span>
        </div>
      </div>

      {/* Sessions header */}
      <div class="flex items-center justify-between px-4 py-2">
        <h2 class="text-xs font-semibold text-[var(--text-tertiary)] uppercase tracking-wider">
          Recent
        </h2>
        <Show when={sessions().length > 0}>
          <span class="text-xs text-[var(--text-muted)] tabular-nums">{sessions().length}</span>
        </Show>
      </div>

      {/* Loading skeleton */}
      <Show when={isLoadingSessions()}>
        <div class="px-3 space-y-2">
          <div class="h-16 bg-[var(--surface-raised)] rounded-[var(--radius-lg)] animate-pulse" />
          <div class="h-16 bg-[var(--surface-raised)] rounded-[var(--radius-lg)] animate-pulse" />
          <div class="h-16 bg-[var(--surface-raised)] rounded-[var(--radius-lg)] animate-pulse" />
        </div>
      </Show>

      {/* Session list */}
      <Show when={!isLoadingSessions()}>
        <div class="flex-1 overflow-y-auto px-3 space-y-1">
          <Show
            when={sessions().length > 0}
            fallback={
              <div class="flex flex-col items-center justify-center py-12 text-center">
                <div
                  class="
                    w-12 h-12 mb-4
                    rounded-[var(--radius-xl)]
                    bg-[var(--surface-raised)]
                    flex items-center justify-center
                  "
                >
                  <MessageSquare class="w-6 h-6 text-[var(--text-muted)]" />
                </div>
                <p class="text-sm font-medium text-[var(--text-secondary)]">No chats yet</p>
                <p class="text-xs text-[var(--text-muted)] mt-1">Start a new conversation</p>
              </div>
            }
          >
            <For each={sessions()}>
              {(session) => (
                <SessionListItem
                  session={session}
                  isActive={currentSession()?.id === session.id}
                  onSelect={() => switchSession(session.id)}
                  onRename={(name) => renameSession(session.id, name)}
                  onArchive={() => archiveSession(session.id)}
                />
              )}
            </For>
          </Show>
        </div>
      </Show>
    </div>
  )
}
