/**
 * Sidebar Panel -- Pencil Design
 *
 * Clean, minimal sidebar with:
 *   1. Action Bar (44px): AVA logo mark + dashboard/search/settings icon buttons
 *   2. New Chat button: Full-width blue-tinted row
 *   3. Project Switcher: Folder + project name + git branch badge + chevrons
 *   4. Thread List: Clean list, no section headers
 *   5. HQ Card: Purple-tinted bottom card
 */

import {
  Archive,
  ArchiveRestore,
  ArrowRight,
  Building2,
  LayoutDashboard,
  MessageSquare,
  Plus,
  Search,
  Settings,
} from 'lucide-solid'
import { type Component, createMemo, createSignal, For, Show } from 'solid-js'
import { useHq } from '../../stores/hq'
import { useLayout } from '../../stores/layout'
import { useSession } from '../../stores/session'
import { ProjectDropdown } from '../sidebar/sessions/ProjectDropdown'
import { SessionItem } from '../sidebar/sessions/SessionItem'
import {
  buildSessionContextMenuItems,
  type ContextMenuState,
} from '../sidebar/sessions/session-context-menu'
import { ConfirmDialog } from '../ui/ConfirmDialog'
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
  const {
    closeProjectHub,
    setRightPanelVisible,
    switchRightPanelTab,
    openSettings,
    dashboardVisible,
    toggleDashboard,
    closeDashboard,
  } = useLayout()
  const { hqMode, toggleHqMode } = useHq()
  const [contextMenu, setContextMenu] = createSignal<ContextMenuState | null>(null)
  const [renameRequest, setRenameRequest] = createSignal<{ id: string; seq: number } | null>(null)
  const [showSearch, setShowSearch] = createSignal(false)
  const [search, setSearch] = createSignal('')
  const [showAll, setShowAll] = createSignal(false)
  const [showArchived, setShowArchived] = createSignal(false)
  const [deleteConfirmId, setDeleteConfirmId] = createSignal<string | null>(null)

  const handleNewChat = async (): Promise<void> => {
    await createNewSession()
    closeProjectHub()
    closeDashboard()
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
      deleteSession: (id) => setDeleteConfirmId(id),
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
        background: 'var(--surface)',
        'border-right': '1px solid var(--border-default)',
        'padding-top': '8px',
        'padding-bottom': '0',
        gap: '4px',
      }}
    >
      {/* 1. Action Bar — 44px */}
      <div class="flex items-center justify-between px-3 flex-shrink-0" style={{ height: '44px' }}>
        {/* AVA logo mark */}
        <div
          class="flex items-center justify-center flex-shrink-0"
          style={{
            width: '26px',
            height: '26px',
            'border-radius': '7px',
            background: 'linear-gradient(180deg, var(--accent), var(--system-purple))',
          }}
        >
          <span class="leading-none text-[12px] font-extrabold text-[var(--text-on-accent)]">
            A
          </span>
        </div>

        {/* Right icon buttons */}
        <div class="flex items-center gap-1">
          <button
            type="button"
            onClick={toggleDashboard}
            class="flex items-center justify-center rounded-[var(--radius-md)] transition-colors"
            classList={{
              'text-[var(--accent)] bg-[var(--accent-subtle)]': dashboardVisible(),
              'text-[var(--text-muted)] hover:text-[var(--text-primary)] hover:bg-[var(--alpha-white-8)]':
                !dashboardVisible(),
            }}
            style={{ width: '30px', height: '30px' }}
            title="Dashboard"
            aria-label="Dashboard"
          >
            <LayoutDashboard class="w-3.5 h-3.5" />
          </button>
          <button
            type="button"
            onClick={toggleSearch}
            class="flex items-center justify-center rounded-[var(--radius-md)] transition-colors"
            classList={{
              'text-[var(--accent)] bg-[var(--accent-subtle)]': showSearch(),
              'text-[var(--text-muted)] hover:text-[var(--text-primary)] hover:bg-[var(--alpha-white-8)]':
                !showSearch(),
            }}
            style={{ width: '30px', height: '30px' }}
            title="Search sessions"
            aria-label="Search sessions"
          >
            <Search class="w-3.5 h-3.5" />
          </button>
          <button
            type="button"
            onClick={openSettings}
            class="flex items-center justify-center rounded-[var(--radius-md)] text-[var(--text-muted)] hover:text-[var(--text-primary)] hover:bg-[var(--alpha-white-8)] transition-colors"
            style={{ width: '30px', height: '30px' }}
            title="Settings (Ctrl+,)"
            aria-label="Settings"
          >
            <Settings class="w-3.5 h-3.5" />
          </button>
        </div>
      </div>

      {/* 2. New Chat button */}
      <div class="px-2 flex-shrink-0">
        <button
          type="button"
          onClick={() => void handleNewChat()}
          class="flex w-full items-center gap-2 px-3 transition-colors hover:bg-[var(--accent-border)]"
          style={{
            height: '36px',
            'border-radius': '8px',
            background: 'var(--accent-subtle)',
            border: '1px solid var(--accent-border)',
          }}
          title="New chat (Ctrl+N)"
          aria-label="New chat"
        >
          <Plus class="h-[15px] w-[15px] text-[var(--accent)]" />
          <span class="text-[13px] font-medium text-[var(--accent)]">New Chat</span>
        </button>
      </div>

      {/* 3. Project Switcher */}
      <div class="px-2 flex-shrink-0">
        <ProjectDropdown />
      </div>

      {/* Inline search (toggled by search icon) */}
      <Show when={showSearch()}>
        <div class="px-2 pb-1 flex-shrink-0">
          <input
            type="text"
            placeholder="Search sessions..."
            value={search()}
            onInput={(e) => setSearch(e.currentTarget.value)}
            onKeyDown={(e) => {
              if (e.key === 'Escape') toggleSearch()
            }}
            autofocus
            aria-label="Search sessions"
            class="
              w-full px-2.5 py-1.5
              text-[13px] text-[var(--text-primary)]
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

      {/* 4. Thread List — clean, no section headers */}
      <PanelErrorBoundary panelName="Sessions">
        <div class="flex-1 overflow-y-auto px-1.5 scrollbar-none">
          <div class="space-y-px">
            <For each={visibleSessions()}>
              {(session) => (
                <SessionItem
                  session={session}
                  isActive={currentSession()?.id === session.id}
                  isBusy={isSessionBusy(session.id)}
                  onSelect={() => {
                    switchSession(session.id)
                    closeDashboard()
                  }}
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
              class="text-[12px] px-2 py-1 mt-1 rounded-[var(--radius-sm)] hover:bg-[var(--alpha-white-5)] transition-colors"
              style={{ color: 'var(--accent)' }}
            >
              Show {hiddenCount()} more
            </button>
          </Show>

          <Show when={showAll() && filteredSessions().length > MAX_VISIBLE_SESSIONS}>
            <button
              type="button"
              onClick={() => setShowAll(false)}
              class="text-[12px] px-2 py-1 mt-1 rounded-[var(--radius-sm)] hover:bg-[var(--alpha-white-5)] transition-colors"
              style={{ color: 'var(--text-muted)' }}
            >
              Show fewer
            </button>
          </Show>

          <Show when={filteredSessions().length === 0}>
            <div class="text-center py-6 px-4" style={{ color: 'var(--text-muted)' }}>
              <MessageSquare class="w-4 h-4 mx-auto mb-1.5 opacity-40" />
              <p class="text-[12px]">{search() ? 'No matching sessions' : 'No chats yet'}</p>
              <Show when={!search()}>
                <p class="text-[11px] mt-0.5 opacity-60">Press Ctrl+N to start</p>
              </Show>
            </div>
          </Show>

          {/* Archived sessions -- inline toggle */}
          <Show when={!search()}>
            <div class="mt-2 mb-1 px-1">
              <button
                type="button"
                onClick={() => {
                  const next = !showArchived()
                  setShowArchived(next)
                  if (next) void loadArchivedSessions()
                }}
                class="inline-flex items-center gap-1 hover:opacity-80 transition-opacity"
                style={{
                  'font-size': '11px',
                  'font-family': "var(--font-ui-mono, 'Geist Mono', ui-monospace, monospace)",
                  color: 'var(--text-muted)',
                  background: 'none',
                  border: 'none',
                  cursor: 'pointer',
                  padding: '2px 4px',
                }}
                title={showArchived() ? 'Hide archived sessions' : 'Show archived sessions'}
              >
                {showArchived()
                  ? 'hide archived'
                  : `${archivedSessions().length || ''} archived`.trim()}
              </button>
            </div>
          </Show>

          {/* Archived sessions list (inline) */}
          <Show when={showArchived()}>
            <div class="space-y-px pb-2">
              <For each={archivedSessions()}>
                {(session) => (
                  <div
                    class="group flex items-center gap-2 px-2 py-1.5 rounded-[var(--radius-md)] transition-colors hover:bg-[var(--alpha-white-5)]"
                    style={{ opacity: '0.6' }}
                  >
                    <Archive class="w-3 h-3 flex-shrink-0" style={{ color: 'var(--text-muted)' }} />
                    <span
                      class="flex-1 truncate"
                      style={{ 'font-size': '12px', color: 'var(--text-muted)' }}
                    >
                      {session.name}
                    </span>
                    <button
                      type="button"
                      onClick={() => runActionSafely(() => unarchiveSession(session.id))}
                      class="p-0.5 rounded-[var(--radius-sm)] text-[var(--text-muted)] hover:text-[var(--text-primary)] hover:bg-[var(--alpha-white-8)] opacity-0 group-hover:opacity-100 transition-opacity"
                      title="Unarchive"
                      aria-label="Unarchive session"
                    >
                      <ArchiveRestore class="w-3 h-3" />
                    </button>
                  </div>
                )}
              </For>
              <Show when={archivedSessions().length === 0}>
                <p class="px-2 py-1 text-center text-[11px] text-[var(--text-muted)]">
                  No archived sessions
                </p>
              </Show>
            </div>
          </Show>
        </div>
      </PanelErrorBoundary>

      {/* 5. HQ Card — bottom, pinned */}
      <div class="flex-shrink-0">
        <button
          type="button"
          onClick={toggleHqMode}
          class="flex w-full items-center justify-between transition-colors hover:bg-[var(--alpha-white-8)]"
          style={{
            padding: '10px 14px',
            background: hqMode()
              ? 'color-mix(in srgb, var(--system-purple) 12%, transparent)'
              : 'color-mix(in srgb, var(--system-purple) 8%, transparent)',
            'border-top': '1px solid color-mix(in srgb, var(--system-purple) 20%, transparent)',
          }}
          title={hqMode() ? 'Switch to Chat' : 'Open HQ'}
          aria-label="Toggle HQ mode"
        >
          <div class="flex items-center gap-2 min-w-0 text-left">
            <Building2 class="w-4 h-4 flex-shrink-0" style={{ color: 'var(--system-purple)' }} />
            <div class="flex flex-col min-w-0" style={{ gap: '1px' }}>
              <span class="leading-tight text-[13px] font-semibold text-[var(--text-primary)]">
                HQ
              </span>
              <span
                class="leading-tight truncate"
                style={{
                  'font-size': '10px',
                  color: 'var(--system-purple)',
                  'font-family': "var(--font-ui-mono, 'Geist Mono', ui-monospace, monospace)",
                }}
              >
                2 agents active
              </span>
            </div>
          </div>
          <ArrowRight
            class="flex-shrink-0"
            style={{ width: '14px', height: '14px', color: 'var(--system-purple)' }}
          />
        </button>
      </div>

      {/* Context Menu */}
      <Show when={contextMenu()}>
        <ContextMenu
          x={contextMenu()!.x}
          y={contextMenu()!.y}
          items={getContextMenuItems(contextMenu()!.sessionId)}
          onClose={() => setContextMenu(null)}
        />
      </Show>

      {/* Delete Confirmation Dialog */}
      <ConfirmDialog
        open={deleteConfirmId() !== null}
        onOpenChange={(open) => {
          if (!open) setDeleteConfirmId(null)
        }}
        title="Delete session?"
        message="This session and all its messages will be permanently deleted. This cannot be undone."
        confirmText="Delete"
        variant="danger"
        onConfirm={() => {
          const id = deleteConfirmId()
          if (id) {
            runActionSafely(() => deleteSessionPermanently(id))
          }
          setDeleteConfirmId(null)
        }}
      />
    </aside>
  )
}
