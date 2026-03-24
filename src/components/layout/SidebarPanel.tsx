/**
 * Unified Sidebar Panel
 *
 * Clean, single-panel sidebar inspired by Goose.
 * Replaces the old ActivityBar + SidebarPanel two-component layout.
 *
 * Layout:
 *   - Top: "New Chat" button + Settings gear
 *   - Search bar
 *   - Session list grouped by date, with compact items
 *   - Archived section at bottom
 */

import { MessageSquare, Plus, Search, Settings } from 'lucide-solid'
import { type Component, createMemo, createSignal, For, Show } from 'solid-js'
import { useLayout } from '../../stores/layout'
import { useSession } from '../../stores/session'
import { ArchivedSection } from '../sidebar/sessions/ArchivedSection'
import { SessionItem } from '../sidebar/sessions/SessionItem'
import {
  buildSessionContextMenuItems,
  type ContextMenuState,
} from '../sidebar/sessions/session-context-menu'
import { groupSessionsByDate } from '../sidebar/sessions/session-utils'
import { ContextMenu } from '../ui/ContextMenu'
import { PanelErrorBoundary } from '../ui/PanelErrorBoundary'

export const SidebarPanel: Component = () => {
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
  } = useSession()
  const { closeProjectHub, setRightPanelVisible, switchRightPanelTab, openSettings } = useLayout()
  const [search, setSearch] = createSignal('')
  const [contextMenu, setContextMenu] = createSignal<ContextMenuState | null>(null)
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

  const runActionSafely = (action: () => Promise<void>): void => {
    void action().catch((error: unknown) => {
      console.error('[SidebarPanel] Action failed', error)
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
    <aside class="flex flex-col h-full w-full overflow-hidden bg-[var(--sidebar-background)] border-r border-[var(--sidebar-border)]">
      {/* Top bar: New Chat + Settings */}
      <div class="flex items-center justify-between px-3 pt-3 pb-1 flex-shrink-0">
        <button
          type="button"
          onClick={() => void handleNewChat()}
          class="
            inline-flex items-center gap-1.5
            px-3 py-1.5
            rounded-[var(--radius-lg)]
            text-[13px] font-medium
            text-[var(--text-primary)]
            hover:bg-[var(--alpha-white-8)]
            transition-colors
          "
          title="New chat (Ctrl+N)"
          aria-label="New chat"
        >
          <Plus class="w-4 h-4 text-[var(--accent)]" />
          <span>New Chat</span>
        </button>

        <button
          type="button"
          onClick={openSettings}
          class="
            flex items-center justify-center
            w-7 h-7 rounded-[var(--radius-md)]
            text-[var(--text-muted)]
            hover:text-[var(--text-primary)]
            hover:bg-[var(--alpha-white-8)]
            transition-colors
          "
          title="Settings (Ctrl+,)"
          aria-label="Settings"
        >
          <Settings class="w-4 h-4" />
        </button>
      </div>

      {/* Search */}
      <div class="px-3 py-2 flex-shrink-0">
        <div class="relative">
          <Search class="absolute left-2.5 top-1/2 -translate-y-1/2 w-3.5 h-3.5 text-[var(--text-muted)]" />
          <input
            type="text"
            placeholder="Search..."
            value={search()}
            onInput={(e) => setSearch(e.currentTarget.value)}
            class="
              w-full pl-8 pr-3 py-1.5
              text-[13px] text-[var(--text-primary)]
              bg-[var(--gray-3)]
              border-none
              rounded-[var(--radius-lg)]
              placeholder:text-[var(--text-muted)]
              focus:outline-none focus:ring-1 focus:ring-[var(--accent)]
              transition-colors
            "
          />
        </div>
      </div>

      {/* Session List */}
      <PanelErrorBoundary panelName="Sessions">
        <div class="flex-1 overflow-y-auto px-1.5 scrollbar-none">
          <div>
            <For each={groupedSessions()}>
              {(group, groupIdx) => (
                <div class={groupIdx() > 0 ? 'mt-3' : ''}>
                  <p class="text-[11px] font-semibold tracking-wider text-[var(--text-muted)] uppercase mb-1 px-2 font-mono">
                    {group.label}
                  </p>
                  <div class="space-y-px">
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
                <MessageSquare class="w-5 h-5 mx-auto mb-2 opacity-40" />
                <p class="text-xs">{search() ? 'No matching sessions' : 'No chats yet'}</p>
                <Show when={!search()}>
                  <p class="text-[10px] mt-1 opacity-60">Press Ctrl+N to start</p>
                </Show>
              </div>
            </Show>
          </div>
        </div>
      </PanelErrorBoundary>

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
    </aside>
  )
}
