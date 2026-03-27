/**
 * Sidebar Panel — Windsurf/Cascade style
 *
 * Clean, minimal sidebar with:
 *   - Top icon bar: new chat, new window actions (left) + search, settings (right)
 *   - Project-grouped session list with folder icon and indentation
 *   - "Show N more sessions" accent-colored link (max 5 visible by default)
 *   - Archived section (collapsible)
 *   - Bottom icon bar pinned to bottom: settings, help, info
 */

import {
  Building2,
  ChevronDown,
  ChevronRight,
  FolderOpen,
  MessageSquare,
  Plus,
  Search,
  Settings,
} from 'lucide-solid'
import { type Component, createMemo, createSignal, For, Show } from 'solid-js'
import { useHq } from '../../stores/hq'
import { useLayout } from '../../stores/layout'
import { useSession } from '../../stores/session'
import { ArchivedSection } from '../sidebar/sessions/ArchivedSection'
import { SessionItem } from '../sidebar/sessions/SessionItem'
import {
  buildSessionContextMenuItems,
  type ContextMenuState,
} from '../sidebar/sessions/session-context-menu'
import { ContextMenu } from '../ui/ContextMenu'
import { PanelErrorBoundary } from '../ui/PanelErrorBoundary'

const MAX_VISIBLE_SESSIONS = 15

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
  const { hqMode, toggleHqMode } = useHq()
  const [contextMenu, setContextMenu] = createSignal<ContextMenuState | null>(null)
  const [renameRequest, setRenameRequest] = createSignal<{ id: string; seq: number } | null>(null)
  const [showSearch, setShowSearch] = createSignal(false)
  const [search, setSearch] = createSignal('')
  const [projectExpanded, setProjectExpanded] = createSignal(true)
  const [showAll, setShowAll] = createSignal(false)

  const handleNewChat = async (): Promise<void> => {
    await createNewSession()
    closeProjectHub()
  }

  const filteredSessions = createMemo(() => {
    const q = search().toLowerCase()
    const all = sessions()
    if (!q) return all
    return all.filter((s) => s.name.toLowerCase().includes(q))
  })

  const visibleSessions = createMemo(() => {
    const all = filteredSessions()
    if (showAll() || all.length <= MAX_VISIBLE_SESSIONS) return all
    return all.slice(0, MAX_VISIBLE_SESSIONS)
  })

  const hiddenCount = createMemo(() => {
    const total = filteredSessions().length
    if (showAll() || total <= MAX_VISIBLE_SESSIONS) return 0
    return total - MAX_VISIBLE_SESSIONS
  })

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

  const toggleSearch = (): void => {
    const next = !showSearch()
    setShowSearch(next)
    if (!next) setSearch('')
  }

  return (
    <aside
      class="flex flex-col h-full w-full overflow-hidden"
      style={{
        background: 'var(--sidebar-background)',
        'border-right': '1px solid var(--sidebar-border)',
      }}
    >
      {/* Top icon bar */}
      <div class="flex items-center justify-between px-3 py-2 flex-shrink-0">
        <div class="flex items-center gap-0.5">
          <button
            type="button"
            onClick={() => void handleNewChat()}
            class="flex items-center justify-center w-7 h-7 rounded-[var(--radius-md)] text-[var(--text-muted)] hover:text-[var(--text-primary)] hover:bg-[var(--alpha-white-8)] transition-colors"
            title="New chat (Ctrl+N)"
            aria-label="New chat"
          >
            <Plus class="w-4 h-4" />
          </button>
          <button
            type="button"
            onClick={toggleHqMode}
            class={`flex items-center justify-center w-7 h-7 rounded-[var(--radius-md)] transition-colors ${
              hqMode()
                ? 'text-[var(--accent)] bg-[var(--accent-subtle)]'
                : 'text-[var(--text-muted)] hover:text-[var(--text-primary)] hover:bg-[var(--alpha-white-8)]'
            }`}
            title={hqMode() ? 'Switch to Chat' : 'Open HQ'}
            aria-label="Toggle HQ mode"
          >
            <Building2 class="w-4 h-4" />
          </button>
        </div>
        <div class="flex items-center gap-0.5">
          <button
            type="button"
            onClick={toggleSearch}
            class={`flex items-center justify-center w-7 h-7 rounded-[var(--radius-md)] transition-colors ${
              showSearch()
                ? 'text-[var(--accent)] bg-[var(--alpha-white-8)]'
                : 'text-[var(--text-muted)] hover:text-[var(--text-primary)] hover:bg-[var(--alpha-white-8)]'
            }`}
            title="Search sessions"
            aria-label="Search sessions"
          >
            <Search class="w-4 h-4" />
          </button>
          <button
            type="button"
            onClick={openSettings}
            class="flex items-center justify-center w-7 h-7 rounded-[var(--radius-md)] text-[var(--text-muted)] hover:text-[var(--text-primary)] hover:bg-[var(--alpha-white-8)] transition-colors"
            title="Settings (Ctrl+,)"
            aria-label="Settings"
          >
            <Settings class="w-4 h-4" />
          </button>
        </div>
      </div>

      {/* Inline search (toggled by search icon) */}
      <Show when={showSearch()}>
        <div class="px-3 pb-2 flex-shrink-0">
          <input
            type="text"
            placeholder="Search sessions..."
            value={search()}
            onInput={(e) => setSearch(e.currentTarget.value)}
            onKeyDown={(e) => {
              if (e.key === 'Escape') toggleSearch()
            }}
            autofocus
            class="
              w-full px-2.5 py-1.5
              text-[var(--text-sm)] text-[var(--text-primary)]
              bg-[var(--gray-3)]
              border-none
              rounded-[var(--radius-md)]
              placeholder:text-[var(--text-muted)]
              focus:outline-none focus:ring-1 focus:ring-[var(--accent)]
              transition-colors
            "
          />
        </div>
      </Show>

      {/* Session list */}
      <PanelErrorBoundary panelName="Sessions">
        <div class="flex-1 overflow-y-auto px-2 scrollbar-none">
          {/* Project group */}
          <div class="mb-3">
            {/* Project header */}
            <button
              type="button"
              onClick={() => setProjectExpanded(!projectExpanded())}
              class="flex items-center gap-1.5 px-2 py-1.5 w-full text-left rounded-[var(--radius-md)] hover:bg-[var(--alpha-white-5)] transition-colors"
            >
              <Show
                when={projectExpanded()}
                fallback={
                  <ChevronRight
                    class="w-3 h-3 flex-shrink-0"
                    style={{ color: 'var(--text-muted)' }}
                  />
                }
              >
                <ChevronDown class="w-3 h-3 flex-shrink-0" style={{ color: 'var(--text-muted)' }} />
              </Show>
              <FolderOpen
                class="w-3.5 h-3.5 flex-shrink-0"
                style={{ color: 'var(--text-secondary)' }}
              />
              <span
                class="text-[var(--text-sm)] font-medium truncate"
                style={{ color: 'var(--text-primary)' }}
              >
                Estela
              </span>
              <Show when={filteredSessions().length > 0}>
                <span
                  class="text-[var(--text-2xs)] ml-auto flex-shrink-0"
                  style={{ color: 'var(--text-muted)' }}
                >
                  {filteredSessions().length}
                </span>
              </Show>
            </button>

            {/* Sessions under this project */}
            <Show when={projectExpanded()}>
              <div class="pl-4 mt-0.5">
                <div class="space-y-px">
                  <For each={visibleSessions()}>
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

                <Show when={hiddenCount() > 0}>
                  <button
                    type="button"
                    onClick={() => setShowAll(true)}
                    class="text-[var(--text-xs)] px-2 py-1 mt-0.5 rounded-[var(--radius-sm)] hover:bg-[var(--alpha-white-5)] transition-colors"
                    style={{ color: 'var(--accent)' }}
                  >
                    Show {hiddenCount()} more sessions
                  </button>
                </Show>

                <Show when={showAll() && filteredSessions().length > MAX_VISIBLE_SESSIONS}>
                  <button
                    type="button"
                    onClick={() => setShowAll(false)}
                    class="text-[var(--text-xs)] px-2 py-1 mt-0.5 rounded-[var(--radius-sm)] hover:bg-[var(--alpha-white-5)] transition-colors"
                    style={{ color: 'var(--text-muted)' }}
                  >
                    Show fewer
                  </button>
                </Show>
              </div>
            </Show>
          </div>

          <Show when={filteredSessions().length === 0}>
            <div class="text-center py-6 px-4" style={{ color: 'var(--text-muted)' }}>
              <MessageSquare class="w-4 h-4 mx-auto mb-1.5 opacity-40" />
              <p class="text-[var(--text-xs)]">
                {search() ? 'No matching sessions' : 'No chats yet'}
              </p>
              <Show when={!search()}>
                <p class="text-[var(--text-2xs)] mt-0.5 opacity-60">Press Ctrl+N to start</p>
              </Show>
            </div>
          </Show>
        </div>
      </PanelErrorBoundary>

      {/* Archived Sessions Section */}
      <ArchivedSection
        archivedSessions={archivedSessions}
        loadArchived={loadArchivedSessions}
        unarchive={(id) => runActionSafely(() => unarchiveSession(id))}
      />

      {/* Bottom spacer — keeps layout clean */}

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
