/**
 * SessionList Component
 * Displays all sessions in the sidebar with create button
 */

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
      {/* New Session button */}
      <button
        type="button"
        onClick={handleCreateSession}
        class="w-full px-4 py-2.5 mb-3 bg-blue-600 hover:bg-blue-500 text-white rounded-lg font-medium transition-colors flex items-center justify-center gap-2"
      >
        <svg
          class="w-5 h-5"
          fill="none"
          stroke="currentColor"
          viewBox="0 0 24 24"
          role="img"
          aria-label="New chat"
        >
          <path
            stroke-linecap="round"
            stroke-linejoin="round"
            stroke-width="2"
            d="M12 4v16m8-8H4"
          />
        </svg>
        New Chat
      </button>

      {/* Sessions header */}
      <div class="flex items-center justify-between px-1 mb-2">
        <h2 class="text-xs font-semibold text-gray-500 uppercase tracking-wider">Recent Chats</h2>
        <Show when={sessions().length > 0}>
          <span class="text-xs text-gray-500">{sessions().length}</span>
        </Show>
      </div>

      {/* Loading skeleton */}
      <Show when={isLoadingSessions()}>
        <div class="space-y-2 animate-pulse">
          <div class="h-16 bg-gray-700 rounded-lg" />
          <div class="h-16 bg-gray-700 rounded-lg" />
          <div class="h-16 bg-gray-700 rounded-lg" />
        </div>
      </Show>

      {/* Session list */}
      <Show when={!isLoadingSessions()}>
        <div class="flex-1 overflow-y-auto space-y-1 -mx-1 px-1">
          <Show
            when={sessions().length > 0}
            fallback={
              <div class="text-center py-8 text-gray-500">
                <p class="text-sm">No chats yet</p>
                <p class="text-xs mt-1">Start a new conversation</p>
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
