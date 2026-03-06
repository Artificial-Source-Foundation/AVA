/**
 * Sidebar Sessions View
 *
 * Session list with search, new chat, and right-click context menu.
 */

import {
  Archive,
  Copy,
  GitBranch,
  GitFork,
  List,
  MessageSquare,
  Pencil,
  Plus,
  Search,
  Trash2,
} from 'lucide-solid'
import { type Component, createMemo, createSignal, For, Show } from 'solid-js'
import { useLayout } from '../../stores/layout'
import { useSession } from '../../stores/session'
import { ContextMenu, type ContextMenuItem } from '../ui/ContextMenu'
import { SessionBranchTree } from './SessionBranchTree'
import { ArchivedSection } from './sessions/ArchivedSection'
import { ProjectDropdown } from './sessions/ProjectDropdown'
import { SessionItem } from './sessions/SessionItem'
import { groupSessionsByDate } from './sessions/session-utils'

interface ContextMenuState {
  x: number
  y: number
  sessionId: string
}

export const SidebarSessions: Component = () => {
  const {
    sessions,
    currentSession,
    createNewSession,
    switchSession,
    deleteSessionPermanently,
    renameSession,
    duplicateSession,
    forkSession,
    archiveSession,
    unarchiveSession,
    archivedSessions,
    loadArchivedSessions,
    isSessionBusy,
    getSessionTree,
  } = useSession()
  const { closeProjectHub } = useLayout()
  const [search, setSearch] = createSignal('')
  const [contextMenu, setContextMenu] = createSignal<ContextMenuState | null>(null)
  const [viewMode, setViewMode] = createSignal<'list' | 'tree'>('list')

  const handleNewChat = async (): Promise<void> => {
    await createNewSession()
    closeProjectHub()
  }

  const filteredSessions = () => {
    const q = search().toLowerCase()
    if (!q) return sessions()
    return sessions().filter((s) => s.name.toLowerCase().includes(q))
  }

  const groupedSessions = createMemo(() => groupSessionsByDate(filteredSessions()))
  const sessionTree = createMemo(() => getSessionTree())

  const handleContextMenu = (e: MouseEvent, sessionId: string): void => {
    e.preventDefault()
    e.stopPropagation()
    setContextMenu({ x: e.clientX, y: e.clientY, sessionId })
  }

  const getContextMenuItems = (sessionId: string): ContextMenuItem[] => [
    {
      label: 'Rename',
      icon: Pencil,
      action: () => {
        /* Rename is handled by SessionItem inline */
      },
    },
    {
      label: 'Duplicate',
      icon: Copy,
      action: () => duplicateSession(sessionId),
    },
    {
      label: 'Fork from here',
      icon: GitFork,
      action: () => {
        const session = sessions().find((s) => s.id === sessionId)
        if (session) forkSession(sessionId, `${session.name} (fork)`)
      },
    },
    { label: '', action: () => {}, separator: true },
    {
      label: 'Archive',
      icon: Archive,
      action: () => void archiveSession(sessionId),
    },
    {
      label: 'Delete',
      icon: Trash2,
      danger: true,
      action: () => void deleteSessionPermanently(sessionId),
    },
  ]

  return (
    <div class="flex flex-col h-full">
      {/* Header: title + project switcher + new chat */}
      <div class="flex items-center justify-between density-px h-10 flex-shrink-0 border-b border-[var(--border-subtle)]">
        <span class="text-xs font-semibold text-[var(--text-secondary)] uppercase tracking-wider">
          Sessions
        </span>

        <div class="flex items-center gap-1">
          <ProjectDropdown />

          {/* Tree/List view toggle */}
          <button
            type="button"
            onClick={() => setViewMode((v) => (v === 'list' ? 'tree' : 'list'))}
            class="
              flex items-center justify-center w-6 h-6
              rounded-[var(--radius-md)]
              text-[var(--text-muted)] hover:text-[var(--text-primary)]
              hover:bg-[var(--alpha-white-8)]
              transition-colors
            "
            title={viewMode() === 'list' ? 'Tree view' : 'List view'}
            aria-label={viewMode() === 'list' ? 'Switch to tree view' : 'Switch to list view'}
          >
            <Show when={viewMode() === 'list'} fallback={<List class="w-4 h-4" />}>
              <GitBranch class="w-4 h-4" />
            </Show>
          </button>

          {/* New chat button */}
          <button
            type="button"
            onClick={handleNewChat}
            class="
              flex items-center justify-center w-6 h-6
              rounded-[var(--radius-md)]
              text-[var(--text-muted)] hover:text-[var(--text-primary)]
              hover:bg-[var(--alpha-white-8)]
              transition-colors
            "
            title="New chat (Ctrl+N)"
            aria-label="New chat"
          >
            <Plus class="w-4 h-4" />
          </button>
        </div>
      </div>

      {/* Search */}
      <div class="density-px density-py flex-shrink-0">
        <div class="relative">
          <Search class="absolute left-2 top-1/2 -translate-y-1/2 w-3.5 h-3.5 text-[var(--text-muted)]" />
          <input
            type="text"
            placeholder="Search sessions..."
            value={search()}
            onInput={(e) => setSearch(e.currentTarget.value)}
            class="
              w-full pl-7 pr-2 py-1.5
              text-xs text-[var(--text-primary)]
              bg-[var(--surface-sunken)]
              border border-[var(--border-subtle)]
              rounded-[var(--radius-md)]
              placeholder:text-[var(--text-muted)]
              focus-glow
            "
          />
        </div>
      </div>

      {/* Session List */}
      <div class="flex-1 overflow-y-auto px-1.5 scrollbar-none">
        <Show when={viewMode() === 'tree'}>
          <SessionBranchTree
            roots={sessionTree().roots}
            childMap={sessionTree().childMap}
            currentSessionId={currentSession()?.id}
            onSelect={(id) => switchSession(id)}
          />
        </Show>
        <Show when={viewMode() === 'list'}>
          <div>
            <For each={groupedSessions()}>
              {(group, groupIdx) => (
                <div class={groupIdx() > 0 ? 'mt-3' : ''}>
                  <p class="text-[10px] font-semibold tracking-wider text-[var(--text-muted)] uppercase mb-1 px-2 font-mono">
                    {group.label}
                  </p>
                  <div class="space-y-0.5">
                    <For each={group.sessions}>
                      {(session) => (
                        <SessionItem
                          session={session}
                          isActive={currentSession()?.id === session.id}
                          isBusy={isSessionBusy(session.id)}
                          onSelect={() => switchSession(session.id)}
                          onRename={(id, name) => renameSession(id, name)}
                          onDelete={(id) => deleteSessionPermanently(id)}
                          onContextMenu={handleContextMenu}
                        />
                      )}
                    </For>
                  </div>
                </div>
              )}
            </For>

            <Show when={filteredSessions().length === 0}>
              <div class="text-center py-8 px-4 text-[var(--text-muted)]">
                <MessageSquare class="w-6 h-6 mx-auto mb-2 opacity-50" />
                <p class="text-xs">{search() ? 'No matching sessions' : 'No chats yet'}</p>
                <Show when={!search()}>
                  <p class="text-[10px] mt-1">Start a new conversation</p>
                </Show>
              </div>
            </Show>
          </div>
        </Show>
      </div>

      {/* Archived Sessions Section */}
      <ArchivedSection
        archivedSessions={archivedSessions}
        loadArchived={loadArchivedSessions}
        unarchive={(id) => void unarchiveSession(id)}
      />

      {/* Context Menu */}
      <Show when={contextMenu()}>
        <ContextMenu
          x={contextMenu()!.x}
          y={contextMenu()!.y}
          items={getContextMenuItems(contextMenu()!.sessionId)}
          onClose={() => setContextMenu(null)}
        />
      </Show>
    </div>
  )
}

/** Skeleton placeholder shown while sessions are loading */
export const SidebarSessionsSkeleton: Component = () => (
  <div class="flex flex-col h-full animate-pulse">
    <div class="flex items-center justify-between px-3 h-10 flex-shrink-0 border-b border-[var(--border-subtle)]">
      <div class="h-3 w-16 bg-[var(--surface-raised)] rounded" />
      <div class="flex gap-1">
        <div class="w-6 h-6 bg-[var(--surface-raised)] rounded-[var(--radius-md)]" />
        <div class="w-6 h-6 bg-[var(--surface-raised)] rounded-[var(--radius-md)]" />
      </div>
    </div>
    <div class="px-3 py-2 flex-shrink-0">
      <div class="h-7 bg-[var(--surface-raised)] rounded-[var(--radius-md)]" />
    </div>
    <div class="flex-1 px-1.5 space-y-1">
      <div class="h-3 w-12 bg-[var(--surface-raised)] rounded mx-2 mb-1" />
      <For each={[1, 2, 3, 4, 5]}>
        {() => (
          <div class="flex items-center gap-2 px-2 py-2 rounded-[var(--radius-md)]">
            <div class="w-3.5 h-3.5 bg-[var(--surface-raised)] rounded" />
            <div class="flex-1 space-y-1">
              <div class="h-3 w-3/4 bg-[var(--surface-raised)] rounded" />
              <div class="h-2.5 w-1/2 bg-[var(--surface-raised)] rounded" />
            </div>
          </div>
        )}
      </For>
    </div>
  </div>
)
