/**
 * Sidebar Sessions View
 *
 * Session list with search, new chat, and right-click context menu.
 */

import { MessageSquare } from 'lucide-solid'
import { type Component, createMemo, createSignal, For, Show } from 'solid-js'
import { useLayout } from '../../stores/layout'
import { useSession } from '../../stores/session'
import { ContextMenu } from '../ui/ContextMenu'
import { SessionBranchTree } from './SessionBranchTree'
import { ArchivedSection } from './sessions/ArchivedSection'
import { SessionItem } from './sessions/SessionItem'
import { SessionsSearch } from './sessions/SessionsSearch'
import { SessionsToolbar } from './sessions/SessionsToolbar'
import {
  buildSessionContextMenuItems,
  type ContextMenuState,
} from './sessions/session-context-menu'
import { groupSessionsByDate } from './sessions/session-utils'

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
  const { closeProjectHub, setRightPanelVisible, switchRightPanelTab } = useLayout()
  const [search, setSearch] = createSignal('')
  const [contextMenu, setContextMenu] = createSignal<ContextMenuState | null>(null)
  const [viewMode, setViewMode] = createSignal<'list' | 'tree'>('list')
  const [renameRequest, setRenameRequest] = createSignal<{ id: string; seq: number } | null>(null)

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
  const sessionTree = getSessionTree

  const runActionSafely = (action: () => Promise<void>): void => {
    void action().catch((error: unknown) => {
      console.error('[SidebarSessions] Action failed', error)
    })
  }

  const handleContextMenu = (e: MouseEvent, sessionId: string): void => {
    e.preventDefault()
    e.stopPropagation()
    setContextMenu({ x: e.clientX, y: e.clientY, sessionId })
  }

  const getContextMenuItems = (sessionId: string) =>
    buildSessionContextMenuItems(sessionId, {
      sessions,
      requestRename: (id) => setRenameRequest({ id, seq: Date.now() }),
      duplicateSession: (id) => runActionSafely(() => duplicateSession(id)),
      forkSession: (id, name) => runActionSafely(() => forkSession(id, name)),
      archiveSession: (id) => runActionSafely(() => archiveSession(id)),
      deleteSession: (id) => runActionSafely(() => deleteSessionPermanently(id)),
      viewTrajectory: (id) => {
        runActionSafely(() => switchSession(id))
        setRightPanelVisible(true)
        switchRightPanelTab('trajectory')
      },
    })

  return (
    <div class="flex flex-col h-full">
      <SessionsToolbar
        viewMode={viewMode()}
        onToggleView={() => setViewMode((value) => (value === 'list' ? 'tree' : 'list'))}
        onNewChat={() => void handleNewChat()}
      />

      <SessionsSearch value={search()} onInput={(event) => setSearch(event.currentTarget.value)} />

      {/* Session List */}
      <div class="flex-1 overflow-y-auto px-1.5 scrollbar-none">
        <Show when={viewMode() === 'tree'}>
          <SessionBranchTree
            roots={sessionTree().roots}
            childMap={sessionTree().childMap}
            currentSessionId={currentSession()?.id}
            onSelect={(id) => runActionSafely(() => switchSession(id))}
          />
        </Show>
        <Show when={viewMode() === 'list'}>
          <div>
            <For each={groupedSessions()}>
              {(group, groupIdx) => (
                <div class={groupIdx() > 0 ? 'mt-3' : ''}>
                  <p class="text-[11px] font-semibold tracking-wider text-[var(--gray-7)] uppercase mb-1 px-2 font-mono">
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
                          renameRequestId={renameRequest()?.id}
                          renameRequestSeq={renameRequest()?.seq}
                          onRenameRequestHandled={() => {
                            setRenameRequest(null)
                          }}
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
        unarchive={(id) => runActionSafely(() => unarchiveSession(id))}
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
